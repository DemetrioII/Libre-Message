#include "net/event_loop/event_loop.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <spdlog/spdlog.h>
#include <stdexcept>

#define MAX_EVENTS 10

EventLoop::EventLoop() {
  epollfd = epoll_create1(0);
  if (epollfd == -1) {
    throw std::runtime_error("error creating event loop");
  }

  wakeupfd = eventfd(0, EFD_NONBLOCK);
  if (wakeupfd == -1) {
    ::close(epollfd);
    throw std::runtime_error("eventfd failed");
  }

  epoll_event ev{};
  ev.events = EPOLLIN;
  ev.data.fd = wakeupfd;

  epoll_ctl(epollfd, EPOLL_CTL_ADD, wakeupfd, &ev);
}

void EventLoop::add_fd(int fd_to_add, std::shared_ptr<EventHandler> handler,
                       uint32_t events) {
  epoll_event ev;
  ev.events = events;
  ev.data.fd = fd_to_add;

  epoll_ctl(epollfd, EPOLL_CTL_ADD, fd_to_add, &ev);

  std::lock_guard<std::mutex> lock(map_mutex_);
  fd_to_handler_[fd_to_add] = handler;
}

void EventLoop::remove_fd(int fd) {
  epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
  std::lock_guard<std::mutex> lock(map_mutex_);
  auto it = fd_to_handler_.find(fd);
  if (it != fd_to_handler_.end())
    fd_to_handler_.erase(fd);
  else
    spdlog::warn("fd={} was not found in the event loop", fd);
}

void EventLoop::run_once() {
  int timeout_ms = 100;
  if (auto next = timer_queue_.next_deadline()) {
    const auto now = TimerQueue::Clock::now();
    if (*next <= now) {
      timeout_ms = 0;
    } else {
      const auto remaining =
          std::chrono::ceil<std::chrono::milliseconds>(*next - now).count();
      timeout_ms = static_cast<int>(std::min<std::int64_t>(remaining, 100));
    }
  }

  epoll_event events[MAX_EVENTS] = {};
  int nfds = epoll_wait(epollfd, events, MAX_EVENTS, timeout_ms);
  if (nfds == -1) {
    if (errno == EINTR) {
      timer_queue_.run_expired(TimerQueue::Clock::now());
      return;
    }
    throw std::runtime_error("epoll_wait failed: " +
                             std::string(strerror(errno)));
  }

  for (int i = 0; i < nfds; ++i) {
    int tmp_fd = events[i].data.fd;
    uint32_t event_mask = events[i].events;

    if (tmp_fd == wakeupfd) {
      uint64_t dummy;
      read(wakeupfd, &dummy, sizeof(dummy));
      continue;
    }

    std::shared_ptr<EventHandler> handler;
    {
      std::lock_guard<std::mutex> lock(map_mutex_);
      auto it = fd_to_handler_.find(tmp_fd);
      if (it == fd_to_handler_.end())
        continue;
      handler = it->second.lock();
    }

    if (handler) {
      handler->handle({.fd = tmp_fd, .events = event_mask});
    }
  }

  timer_queue_.run_expired(TimerQueue::Clock::now());
}

void EventLoop::modify_fd(int fd, uint32_t events) {
  epoll_event ev{};
  ev.events = events;
  ev.data.fd = fd;
  epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::wakeup() {
  std::uint64_t one = 1;
  while (::write(wakeupfd, &one, sizeof(one)) == -1) {
    if (errno == EINTR)
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return;
    spdlog::error("eventfd wakeup failed: {}", strerror(errno));
    return;
  }
}

TimerId EventLoop::schedule_timer(TimerQueue::TimePoint deadline,
                                  TimerQueue::Callback callback) {
  auto result = timer_queue_.schedule(deadline, std::move(callback));
  if (result.became_earlier)
    wakeup();
  return result.id;
}

bool EventLoop::cancel_timer(TimerId id) { return timer_queue_.cancel(id); }

EventLoop::~EventLoop() {
  for (auto &[fd, handler] : fd_to_handler_) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, nullptr);
  }
  ::close(epollfd);
  ::close(wakeupfd);
}
