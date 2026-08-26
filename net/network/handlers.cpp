#include "net/network/handlers.hpp"
#include <iostream>
#include <sys/epoll.h>

TCPListenerEventHandler::~TCPListenerEventHandler() {}

TCPListenerEventHandler::TCPListenerEventHandler(
    std::shared_ptr<TCPListener> listener,
    const std::function<void(TCPConnection &&)> connection_callback)
    : listener_(listener), connection_callback_(connection_callback) {}

void TCPListenerEventHandler::handle(const Event &events) {
  if (!(events.events & EPOLLIN))
    return;

  auto connection = listener_.lock()->accept();

  if (!connection) {
    // handle error
    return;
  }

  std::cout << "CONNECTED PEER WITH FILE DESCRIPTOR="
            << connection->socket().get_fd() << std::endl;
  connection_callback_(std::move(*connection));
}

TCPConnectionEventHandler::TCPConnectionEventHandler(
    int fd, std::shared_ptr<TCPListener> listener,
    std::shared_ptr<TCPConnection> connection,
    const std::function<void(int, uint32_t)> &enable_writing,
    const std::function<void(int fd)> &on_readable,
    const std::function<void(int fd)> &on_writable,
    const std::function<void(int fd)> &on_disconnected)
    : listener_(listener), fd_(fd), connection_(connection),
      on_readable_(on_readable), on_writable_(on_writable),
      on_disconnected_(on_disconnected), enable_writing_(enable_writing) {}

void TCPConnectionEventHandler::handle_disconnect() {
  std::cout << "PEER WITH FD=" << fd_ << " WAS DISCONNECTED!" << std::endl;
}

void TCPConnectionEventHandler::handle(const Event &events) {
  if (events.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
    handle_disconnect();
    on_disconnected_(fd_);
    return;
  }

  if (events.events & EPOLLIN) {
    on_readable_(fd_);
  }

  if (events.events & EPOLLOUT) {
    on_writable_(fd_);
  }
}

TCPConnectionEventHandler::~TCPConnectionEventHandler() {}
