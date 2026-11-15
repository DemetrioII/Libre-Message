#pragma once
#include "net/event_loop/event_handler.hpp"
#include "net/event_loop/timer_queue.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

class EventLoop {
  int epollfd{-1};
  int wakeupfd{-1};
  std::mutex map_mutex_;
  std::unordered_map<int, std::weak_ptr<EventHandler>> fd_to_handler_;
  TimerQueue timer_queue_;

public:
  EventLoop();

  void remove_fd(int fd);

  void add_fd(int fd_to_add, std::shared_ptr<EventHandler> handler,
              uint32_t events);

  void modify_fd(int fd, uint32_t events);

  void run_once();

  void wakeup();

  TimerId schedule_timer(TimerQueue::TimePoint deadline,
                         TimerQueue::Callback callback);

  bool cancel_timer(TimerId id);

  ~EventLoop();
};
