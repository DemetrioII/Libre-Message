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

void TCPPeer::on_read(ConnectionId id) {
  IOStatus status;
  std::shared_ptr<TCPConnection> connection_ptr;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto id_it = id_to_fd_.find(id);
    if (id_it == id_to_fd_.end()) {
      spdlog::debug("stale read event for connection id={}", id);
      return;
    }

    auto it = connections_.find(id_it->second);
    if (it == connections_.end() || it->second.id != id) {
      spdlog::debug("stale read event for connection id={}", id);
      return;
    }

    connection_ptr = it->second.connection;
  }
  const auto state = connection_ptr->state();
  if (state == ConnectionState::Connecting) {
    spdlog::debug("read event before connect completion for connection id={}",
                  id);
    return;
  } else if (state == ConnectionState::TlsHandshaking) {
    handle_handshake_result(id, connection_ptr->continue_handshake());
    return;
  } else if (state == ConnectionState::Closing) {
    while (auto message = connection_ptr->next_message()) {
      spdlog::info("received complete frame ({} bytes) on id={}",
                   message->size(), id);

      const auto decoded = MessageCodec::decode(*message);
      if (!decoded) {
        spdlog::warn("failed to decode message on id={} (error {})", id,
                     static_cast<int>(decoded.error()));
        continue;
      }

      if (on_message_) {
        on_message_(id, std::move(*decoded));
      }
    }
    on_disconnect(id);
    return;
  }
  status = connection_ptr->receive();
  if (status == IOStatus::Error) {
    spdlog::error("read error on connection with id={}", id);
    on_disconnect(id);
    return;
  }
  if (status == IOStatus::Complete)
    mark_activity(id);

  while (auto message = connection_ptr->next_message()) {
    spdlog::info("received complete frame ({} bytes) on id={}", message->size(),
                 id);

    const auto decoded = MessageCodec::decode(*message);
    if (!decoded) {
      spdlog::warn("failed to decode message on id={} (error {})", id,
                   static_cast<int>(decoded.error()));
      continue;
    }

    if (on_message_) {
      on_message_(id, std::move(*decoded));
    }
  }

  if (status == IOStatus::Closed) {
    spdlog::info("connection with id={} closed", id);
    on_disconnect(id);
  }
}

void TCPPeer::set_sender_id(const std::string &id) { sender_id_ = id; }

void TCPPeer::set_message_handler(
    std::function<void(ConnectionId, MessageModel)> handler) {
  on_message_ = std::move(handler);
}

void TCPPeer::set_disconnect_handler(
    std::function<void(ConnectionId)> handler) {
  on_disconnect_ = std::move(handler);
}

std::vector<std::byte> TCPPeer::make_handshake_message() const {
  libre_message::Message m;
  m.set_id("handshake");
  m.set_sender_id(sender_id_);
  m.set_timestamp(0);
  m.mutable_handshake()->set_sender_id(sender_id_);
  return MessageCodec::encode(MessageModel(std::move(m)));
}

void TCPPeer::on_write(ConnectionId id) {
  int fd = -1;
  std::shared_ptr<TCPConnection> connection_ptr;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto id_it = id_to_fd_.find(id);
    if (id_it == id_to_fd_.end()) {
      spdlog::debug("stale write event for connection id={}", id);
      return;
    }

    fd = id_it->second;
    auto it = connections_.find(fd);
    if (it == connections_.end() || it->second.id != id) {
      spdlog::debug("stale write event for connection id={}", id);
      return;
    }

    connection_ptr = it->second.connection;
  }

  const auto state = connection_ptr->state();
  if (state == ConnectionState::Connecting) {
    int err = 0;
    socklen_t len = sizeof(err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
      handle_handshake_result(id, connection_ptr->continue_handshake());
      return;
    } else {
      on_disconnect(id);
      return;
    }
  }
  if (state == ConnectionState::TlsHandshaking) {
    handle_handshake_result(id, connection_ptr->continue_handshake());
    return;
  }
  if (state == ConnectionState::Closing) {
    on_disconnect(id);
    return;
  }

  if (!connection_ptr->has_to_write())
    return;

  auto res = connection_ptr->flush();
  if (res.bytes_sent > 0)
    mark_activity(id);
  if (res.status == IOStatus::Error) {
    spdlog::error("flush error on connection with id={}", id);
    on_disconnect(id);
    return;
  }
}

void TCPPeer::maybe_cancel_establishment_timer(ConnectionId id) {
  std::optional<TimerId> timer;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto id_it = id_to_fd_.find(id);
    if (id_it == id_to_fd_.end())
      return;

    auto it = connections_.find(id_it->second);
    if (it == connections_.end() || it->second.id != id)
      return;

    if (it->second.connection->state() != ConnectionState::Established)
      return;

    timer = it->second.establishment_timer;
    it->second.establishment_timer.reset();
  }

  if (timer)
    event_loop_->cancel_timer(*timer);
}

void TCPPeer::on_established(ConnectionId id) {
  std::lock_guard<std::mutex> lock(connection_map_mutex_);
  auto id_it = id_to_fd_.find(id);
  if (id_it == id_to_fd_.end())
    return;

  auto it = connections_.find(id_it->second);
  if (it == connections_.end() || it->second.id != id)
    return;

  if (it->second.connection->state() != ConnectionState::Established)
    return;

  if (it->second.idle_timer ||
      options_.idle_timeout <= std::chrono::milliseconds::zero())
    return;

  it->second.last_activity = TimerQueue::Clock::now();
  it->second.idle_timer = event_loop_->schedule_timer(
      it->second.last_activity + options_.idle_timeout,
      [this, id] { on_idle_timeout(id); });
}

void TCPPeer::on_idle_timeout(ConnectionId id) {
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto id_it = id_to_fd_.find(id);
    if (id_it == id_to_fd_.end())
      return;

    auto it = connections_.find(id_it->second);
    if (it == connections_.end() || it->second.id != id)
      return;

    it->second.idle_timer.reset();

    if (it->second.connection->state() != ConnectionState::Established)
      return;

    const auto now = TimerQueue::Clock::now();
    if (now - it->second.last_activity < options_.idle_timeout) {
      it->second.idle_timer = event_loop_->schedule_timer(
          it->second.last_activity + options_.idle_timeout,
          [this, id] { on_idle_timeout(id); });
      return;
    }
  }

  spdlog::warn("idle timeout for connection id={}", id);
  on_disconnect(id);
}

void TCPPeer::mark_activity(ConnectionId id) {
  std::lock_guard<std::mutex> lock(connection_map_mutex_);
  auto id_it = id_to_fd_.find(id);
  if (id_it == id_to_fd_.end())
    return;

  auto it = connections_.find(id_it->second);
  if (it == connections_.end() || it->second.id != id)
    return;

  it->second.last_activity = TimerQueue::Clock::now();
}

void TCPPeer::handle_handshake_result(ConnectionId id,
                                      const FlushResult &result) {
  if (result.status == IOStatus::Complete) {
    maybe_cancel_establishment_timer(id);
    on_established(id);
  } else if (result.status == IOStatus::Closed ||
             result.status == IOStatus::Error) {
    on_disconnect(id);
  }
}

void TCPPeer::on_establishment_timeout(ConnectionId id) {
  std::shared_ptr<TCPConnection> connection;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto id_it = id_to_fd_.find(id);
    if (id_it == id_to_fd_.end())
      return;

    auto it = connections_.find(id_it->second);
    if (it == connections_.end() || it->second.id != id)
      return;

    connection = it->second.connection;
    it->second.establishment_timer.reset();
  }

  const auto state = connection->state();
  if (state != ConnectionState::Connecting &&
      state != ConnectionState::TlsHandshaking) {
    return;
  }

  spdlog::warn("establishment timeout for connection id={}", id);
  on_disconnect(id);
}

void TCPPeer::on_disconnect(ConnectionId id) {
  int fd = -1;
  std::shared_ptr<TCPConnection> connection;
  std::optional<TimerId> establishment_timer;
  std::optional<TimerId> idle_timer;
  {
    std::lock_guard lock(connection_map_mutex_);
    auto id_it = id_to_fd_.find(id);
    if (id_it == id_to_fd_.end()) {
      spdlog::debug("stale disconnect event for connection id={}", id);
      return;
    }

    fd = id_it->second;
    auto it = connections_.find(fd);
    if (it == connections_.end() || it->second.id != id) {
      spdlog::debug("stale disconnect event for connection id={}", id);
      id_to_fd_.erase(id_it);
      return;
    }

    connection = it->second.connection;
    establishment_timer = it->second.establishment_timer;
    idle_timer = it->second.idle_timer;
    id_to_fd_.erase(id_it);
    connections_.erase(it);
    connection_handlers_.erase(fd);
  }

  if (establishment_timer)
    event_loop_->cancel_timer(*establishment_timer);
  if (idle_timer)
    event_loop_->cancel_timer(*idle_timer);

  event_loop_->remove_fd(fd);
  if (on_disconnect_)
    on_disconnect_(id);
}

ConnectionId TCPPeer::make_tcp_connection(TCPConnection &&connection) {
  int fd = connection.socket().get_fd();
  ConnectionId id = kInvalidConnectionId;
  {
    std::lock_guard<std::mutex> lock{connection_map_mutex_};
    id = next_connection_id_++;
    auto connection_ptr =
        std::make_shared<TCPConnection>(std::move(connection));
    spdlog::info("connection with fd={} was added with id={}", fd, id);
    connection_ptr->set_write_interest_handler([this, fd](bool enable) {
      event_loop_->modify_fd(
          fd, enable ? EPOLLIN | EPOLLERR | EPOLLOUT | EPOLLHUP | EPOLLRDHUP
                     : EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP);
    });

    std::shared_ptr<TCPConnectionEventHandler> event_handler =
        std::make_shared<TCPConnectionEventHandler>(
            fd, listener_, connection_ptr,
            [this](int fd, uint32_t events) {
              event_loop_->modify_fd(fd, events);
            },
            [this, id](int) { on_read(id); }, [this, id](int) { on_write(id); },
            [this, id](int) { on_disconnect(id); });

    connections_[fd] = ConnectionEntry{id, connection_ptr};
    id_to_fd_[id] = fd;
    connection_handlers_[fd] = event_handler;

    if (options_.establishment_timeout > std::chrono::milliseconds::zero()) {
      auto timer = event_loop_->schedule_timer(
          TimerQueue::Clock::now() + options_.establishment_timeout,
          [this, id] { on_establishment_timeout(id); });
      connections_[fd].establishment_timer = timer;
    }

    event_loop_->add_fd(fd, event_handler,
                        EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLHUP);
  }
  if (!sender_id_.empty()) {
    std::vector<std::byte> handshake = make_handshake_message();
    send_data(id, handshake);
  }
  return id;
}

TCPPeer::TCPPeer(std::string_view host, short port, int ip_version,
                 bool use_tls, TCPPeerOptions options)
    : use_tls_(use_tls), options_(options),
      event_loop_(std::make_unique<EventLoop>()),
      endpoint_(EndpointManager::Construct(ip_version, host, port)),
      listener_(bind_listener(endpoint_, use_tls_)),
      tcp_listener_handler_(std::make_shared<TCPListenerEventHandler>(
          listener_, [this](TCPConnection &&connection) {
            make_tcp_connection(std::move(connection));
          })) {

  event_loop_->add_fd(listener_->socket().get_fd(), tcp_listener_handler_,
                      EPOLLIN | EPOLLERR);
}

TCPPeer::~TCPPeer() {
  event_loop_jthread_.request_stop();
  event_loop_->wakeup();
  if (event_loop_jthread_.joinable())
    event_loop_jthread_.join();
}

void TCPPeer::run_event_loop() {
  event_loop_jthread_ = std::jthread([this](std::stop_token stop_token) {
    while (!stop_token.stop_requested())
      event_loop_->run_once();
  });
}

void TCPPeer::send_data(ConnectionId id, std::span<std::byte> data) {
  std::shared_ptr<TCPConnection> connection_ptr;
  int fd = -1;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    auto id_it = id_to_fd_.find(id);
    if (id_it == id_to_fd_.end()) {
      spdlog::warn("send_data: connection with id={} was not found", id);
      return;
    }

    fd = id_it->second;
    auto it = connections_.find(fd);
    if (it == connections_.end() || it->second.id != id) {
      spdlog::warn("send_data: stale connection with id={}", id);
      return;
    }

    connection_ptr = it->second.connection;
  }

  spdlog::info("sending data ({} bytes) to peer with id={}", data.size(), id);
  connection_ptr->send(data);
  mark_activity(id);

  // event_loop_->modify_fd(fd, EPOLLIN | EPOLLERR | EPOLLOUT | EPOLLHUP |
  // EPOLLRDHUP);
}

void TCPPeer::broadcast(std::span<std::byte> data) {
  std::vector<std::pair<int, std::shared_ptr<TCPConnection>>> targets;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
    for (const auto &[fd, connection] : connections_)
      targets.emplace_back(fd, connection.connection);
  }

  for (const auto &[fd, connection] : targets) {
    connection->send(data);
    // event_loop_->modify_fd(fd, EPOLLIN | EPOLLERR | EPOLLOUT | EPOLLHUP |
    // EPOLLRDHUP);
  }
}

ConnectionId TCPPeer::connect(std::string_view host, short port,
                              int ip_version) {
  Endpoint endpoint = EndpointManager::Construct(ip_version, host, port);
  Socket socket =
      ::socket(ip_version == 6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
  auto connection = TCPConnection::connect(std::move(socket), endpoint,
                                           use_tls_, Role::Client);
  if (!connection)
    return kInvalidConnectionId;

  // connection->continue_handshake();

  // connection->set_tls_connection();
  return make_tcp_connection(std::move(*connection));
}

void TCPPeer::disconnect(std::string_view host, short port) {
  ConnectionId id = kInvalidConnectionId;
  {
    std::lock_guard<std::mutex> lock(connection_map_mutex_);
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
              pair.second.connection->endpoint());
        });

    if (it != connections_.end())
      id = it->second.id;
    spdlog::info("disconnecting from peer {}:{}", host, port);
  }
  if (id != kInvalidConnectionId)
    on_disconnect(id);
}
