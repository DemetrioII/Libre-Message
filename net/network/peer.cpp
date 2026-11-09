#include "net/network/peer.hpp"
#include "models/message_codec.hpp"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <vector>

static std::shared_ptr<TCPListener> bind_listener(const Endpoint &endpoint,
                                                  bool use_tls) {
  auto listener = TCPListener::bind(endpoint, use_tls);
  if (!listener)
    throw std::runtime_error("failed to bind TCP listener");
  return std::make_shared<TCPListener>(std::move(*listener));
}

void TCPPeer::on_read(int fd) {
  IOStatus status;
  std::shared_ptr<TCPConnection> connection_ptr;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto it = connections_.find(fd);

    if (it == connections_.end()) {
      spdlog::error("connection with fd={} was not found", fd);
      on_disconnect(fd);
      return;
    }

    connection_ptr = it->second;
  }
  status = connection_ptr->receive();
  if (status == IOStatus::Error) {
    spdlog::error("read error on connection with fd={}", fd);
    return;
  }
  if (status == IOStatus::Closed) {
    spdlog::info("connection with fd={} closed", fd);
    return;
  }

  while (auto message = connection_ptr->next_message()) {
    spdlog::info("received complete frame ({} bytes) on fd={}", message->size(),
                 fd);

    const auto decoded = MessageCodec::decode(*message);
    if (!decoded) {
      spdlog::warn("failed to decode message on fd={} (error {})", fd,
                   static_cast<int>(decoded.error()));
      continue;
    }

    if (decoded->is_text())
      spdlog::info("received text message from {}: {}",
                   decoded->proto().sender_id(), decoded->text());

    if (on_message_) {
      on_message_(fd, std::move(*decoded));
    }
  }
}

void TCPPeer::set_sender_id(std::string id) { sender_id_ = std::move(id); }

void TCPPeer::set_message_handler(
    std::function<void(int, MessageModel)> handler) {
  on_message_ = std::move(handler);
}

std::vector<std::byte> TCPPeer::make_handshake_message() const {
  libre_message::Message m;
  m.set_id("handshake");
  m.set_sender_id(sender_id_);
  m.set_timestamp(0);
  m.mutable_handshake()->set_sender_id(sender_id_);
  return MessageCodec::encode(MessageModel(std::move(m)));
}

void TCPPeer::on_write(int fd) {
  std::shared_ptr<TCPConnection> connection_ptr;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
      spdlog::error("error with fd={} was not found", fd);
      return;
    }
    connection_ptr = it->second;
  }

  if (!connection_ptr->has_to_write())
    return;

  auto res = connection_ptr->flush();
  if (res.status == IOStatus::Error) {
    spdlog::error("flush error on fd={}", fd);
    on_disconnect(fd);
    return;
  }

  if (!connection_ptr->has_to_write())
    event_loop_->modify_fd(fd, EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
}

void TCPPeer::on_disconnect(int fd) {
  event_loop_->remove_fd(fd);

  std::lock_guard<std::mutex> lock(connection_map_mutex_);
  connections_.erase(fd);
  connection_handlers_.erase(fd);
}

void TCPPeer::make_tcp_connection(TCPConnection &&connection) {
  int fd = connection.socket().get_fd();
  {
    std::lock_guard<std::mutex> lock{connection_map_mutex_};
    auto connection_ptr =
        std::make_shared<TCPConnection>(std::move(connection));
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
  if (!sender_id_.empty()) {
    std::vector<std::byte> handshake = make_handshake_message();
    send_data(fd, handshake);
  }
}

TCPPeer::TCPPeer(std::string_view host, short port, int ip_version,
                 bool use_tls)
    : use_tls_(use_tls), event_loop_(std::make_unique<EventLoop>()),
      endpoint_(EndpointManager::Construct(ip_version, host, port)),
      listener_(bind_listener(endpoint_, use_tls_)),
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
  std::shared_ptr<TCPConnection> connection_ptr;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto it = connections_.find(fd);
    if (it == connections_.end()) {
      spdlog::warn("send_data: connection with fd={} was not found", fd);
      return;
    }
    connection_ptr = it->second;
  }

  spdlog::info("sending data ({} bytes) to peer with fd={}", data.size(), fd);
  connection_ptr->send(data);
  event_loop_->modify_fd(fd,
                         EPOLLIN | EPOLLERR | EPOLLOUT | EPOLLHUP | EPOLLRDHUP);
}

void TCPPeer::broadcast(std::span<std::byte> data) {
  std::vector<std::pair<int, std::shared_ptr<TCPConnection>>> targets;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    for (const auto &[fd, connection] : connections_)
      targets.emplace_back(fd, connection);
  }

  for (const auto &[fd, connection] : targets) {
    connection->send(data);
    event_loop_->modify_fd(fd, EPOLLIN | EPOLLERR | EPOLLOUT | EPOLLHUP |
                                   EPOLLRDHUP);
  }
}

int TCPPeer::connect(std::string_view host, short port, int ip_version) {
  Endpoint endpoint = EndpointManager::Construct(ip_version, host, port);
  Socket socket =
      ::socket(ip_version == 6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  auto connection =
      TCPConnection::connect(std::move(socket), endpoint, use_tls_);
  if (!connection)
    return -1;

  int fd = connection->socket().get_fd();
  connection->set_tls_connection();
  make_tcp_connection(std::move(*connection));
  return fd;
}

void TCPPeer::disconnect(std::string_view host, short port) {
  std::lock_guard<std::mutex> lock(connection_map_mutex_);
  int fd = -1;
  {
    auto it = std::find_if(
        connections_.begin(), connections_.end(),
        [host, port](const auto &pair) {
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

    if (it != connections_.end()) {
      fd = it->first;
      connections_.erase(it);
    }
    spdlog::info("disconnecting from peer {}:{}", host, port);
  }
  if (fd != -1) {
    auto it = connection_handlers_.find(fd);
    if (it != connection_handlers_.end())
      connection_handlers_.erase(it);
  }
  if (fd != -1) {
    event_loop_->remove_fd(fd);
  }
}
