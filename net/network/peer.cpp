#include "net/network/peer.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

void TCPPeer::on_read(int fd) {
  auto connection_ptr = connections_[fd];
  std::array<std::byte, 1024> data;
  const auto n_bytes = connection_ptr->recv(data);
  if (!n_bytes) {
    spdlog::error("error reading on connection with fd={}", fd);
    return;
  }
  for (int i = 0; i < *n_bytes; ++i)
    std::cout << std::hex << static_cast<char>(data[i]);
  std::cout << std::endl;
}

void TCPPeer::on_write(int fd) {
  auto connection_ptr = connections_[fd];
  if (connection_ptr->has_to_write()) {
    auto res = connection_ptr->flush();
    if (res.status != IOStatus::Error)
      return;
    event_loop_->modify_fd(fd, EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
  }
}

void TCPPeer::on_disconnect(int fd) {
  event_loop_->remove_fd(fd);
  connections_.erase(fd);
  connection_handlers_.erase(fd);
}

void TCPPeer::make_tcp_connection(TCPConnection &&connection) {
  std::lock_guard<std::mutex> lock{connection_map_mutex_};
  auto connection_ptr = std::make_shared<TCPConnection>(std::move(connection));
  spdlog::info("connection with fd={} was added to map",
               connection_ptr->socket().get_fd());
  std::shared_ptr<TCPConnectionEventHandler> event_handler =
      std::make_shared<TCPConnectionEventHandler>(
          connection_ptr->socket().get_fd(), listener_, connection_ptr,
          [this](int fd, uint32_t events) {
            event_loop_->modify_fd(fd, events);
          },
          [this](int fd) { on_read(fd); }, [this](int fd) { on_write(fd); },
          [this](int fd) { on_disconnect(fd); });
  connection_handlers_[connection_ptr->socket().get_fd()] = event_handler;
  event_loop_->add_fd(connection_ptr->socket().get_fd(), event_handler,
                      EPOLLIN | EPOLLRDHUP | EPOLLERR | EPOLLHUP);
  connections_[connection_ptr->socket().get_fd()] = connection_ptr;
}

TCPPeer::TCPPeer(std::string_view host, short port, int ip_version,
                 bool use_tls)
    : use_tls_(use_tls), event_loop_(std::make_unique<EventLoop>()),
      endpoint_(EndpointManager::Construct(ip_version, host, port)),
      listener_(std::make_shared<TCPListener>(
          *TCPListener::bind(endpoint_, use_tls_))),
      tcp_listener_handler_(std::make_shared<TCPListenerEventHandler>(
          listener_, [this](TCPConnection &&connection) {
            make_tcp_connection(std::move(connection));
          })) {

  event_loop_->add_fd(listener_->socket().get_fd(), tcp_listener_handler_,
                      EPOLLIN | EPOLLERR);
}

void TCPPeer::run_event_loop() {
  event_loop_jthread_ = std::jthread([this](std::stop_token stop_token) {
    while (!stop_token.stop_requested())
      event_loop_->run_once();
  });
}

void TCPPeer::send_data(int fd, std::span<std::byte> data) {
  spdlog::info("sending data ({} bytes) to peer with fd={}", data.size(), fd);
  connections_[fd]->send(data);
  event_loop_->modify_fd(fd,
                         EPOLLIN | EPOLLERR | EPOLLOUT | EPOLLHUP | EPOLLRDHUP);
}

void TCPPeer::broadcast(std::span<std::byte> data) {
  std::lock_guard<std::mutex> lock{connection_map_mutex_};
  for (const auto &[fd, connection] : connections_) {
    send_data(fd, data);
  }
}

int TCPPeer::connect(std::string_view host, short port, int ip_version) {
  Endpoint endpoint = EndpointManager::Construct(ip_version, host, port);
  Socket socket = ::socket(AF_INET, SOCK_STREAM, 0);
  auto connection =
      TCPConnection::connect(std::move(socket), endpoint, use_tls_);
  connection->set_tls_connection();
  if (!connection)
    return -1;

  int fd = connection->socket().get_fd();
  make_tcp_connection(std::move(*connection));
  return fd;
}

void TCPPeer::disconnect(std::string_view host, short port) {
  auto it = std::find_if(
      connections_.begin(), connections_.end(), [host, port](const auto &pair) {
        return std::visit(
            [host, port](const auto &ep) {
              if constexpr (std::is_same_v<std::decay_t<decltype(ep)>,
                                           IPv4Endpoint>) {
                uint16_t ep_port = ntohs(ep.address.sin_port);
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &ep.address.sin_addr, ip_str,
                          INET_ADDRSTRLEN);
                return std::string_view(ip_str) == host && ep_port == port;
              }

              else if constexpr (std::is_same_v<std::decay_t<decltype(ep)>,
                                                IPv6Endpoint>) {
                uint16_t ep_port = ntohs(ep.address.sin6_port);
                char ip_str[INET6_ADDRSTRLEN];
                inet_ntop(AF_INET6, &ep.address.sin6_addr, ip_str,
                          INET6_ADDRSTRLEN);
                return std::string_view(ip_str) == host && ep_port == port;
              }

              return false;
            },
            pair.second->endpoint());
      });

  if (it != connections_.end())
    connections_.erase(it);
  spdlog::info("disconnecting from peer {}:{}", host, port);
}
