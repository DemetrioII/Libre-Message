#include "net/network/handlers.hpp"
#include <iostream>
#include <spdlog/spdlog.h>
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
    spdlog::error("accept on listener was failed with connection");
    return;
  }

  connection->set_tls_acception();

  spdlog::info("connected peer with fd {}", connection->socket().get_fd());
  connection_callback_(std::move(*connection));
}

TCPConnectionEventHandler::TCPConnectionEventHandler(
    int fd, std::shared_ptr<TCPListener> listener,
    std::shared_ptr<TCPConnection> connection,
    const ModifyEventsHandler &enable_writing, const ReadHandler &on_readable,
    const WriteHandler &on_writable, const DisconnectHandler &on_disconnected)
    : listener_(listener), fd_(fd), connection_(connection),
      on_readable_(on_readable), on_writable_(on_writable),
      on_disconnected_(on_disconnected), modify_events_(enable_writing) {}

void TCPConnectionEventHandler::handle_disconnect() {
  spdlog::info("peer with fd {} was disconnected",
               connection_.lock()->socket().get_fd());
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
