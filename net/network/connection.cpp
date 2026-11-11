#include "net/network/connection.hpp"
#include "models/framer.hpp"
#include <errno.h>
#include <iostream>
#include <spdlog/spdlog.h>

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

TCPTransport make_transport(int fd, bool use_tls) {
  if (use_tls)
    return TLSTransport(fd);
  else
    return PlainTCPTransport(fd);
}

template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

TCPConnection::TCPConnection(Socket &&socket, const Endpoint &endpoint,
                             bool use_tls, Role role)
    : socket_(std::move(socket)), endpoint_(endpoint), role_(role),
      transport_(make_transport(socket_.get_fd(), use_tls)) {
  if (use_tls) {
    std::visit(
        [role](auto &transport) {
          if constexpr (std::is_same_v<std::decay_t<decltype(transport)>,
                                       TLSTransport>) {
            if (role == Role::Client)
              transport.set_role(1);
            else
              transport.set_role(0);
          }
        },
        transport_);
  }
}

TCPConnection::TCPConnection(TCPConnection &&other) noexcept
    : socket_(std::move(other.socket_)), endpoint_(std::move(other.endpoint_)),
      role_(other.role_), send_buffer_(std::move(other.send_buffer_)),
      receive_buffer_(std::move(other.receive_buffer_)),
      transport_(std::move(other.transport_)), state_(other.state_.load()),
      write_interest_handler_(std::move(other.write_interest_handler_)),
      has_to_write_(other.has_to_write_.load()) {}

TCPConnection &TCPConnection::operator=(TCPConnection &&other) noexcept {
  if (this != &other) {
    socket_ = std::move(other.socket_);
    endpoint_ = std::move(other.endpoint_);
    role_ = other.role_;
    send_buffer_ = std::move(other.send_buffer_);
    receive_buffer_ = std::move(other.receive_buffer_);
    transport_ = std::move(other.transport_);
    state_.store(other.state_.load());
    write_interest_handler_ = std::move(other.write_interest_handler_);
    has_to_write_.store(other.has_to_write_.load());
  }
  return *this;
}

ConnectionState TCPConnection::state() { return state_.load(); }

FlushResult TCPConnection::continue_handshake() {
  if (state_.load() == ConnectionState::Closing)
    return {0, IOStatus::Error};
  if (state_.load() == ConnectionState::Established)
    return {0, IOStatus::Complete};
  state_ = ConnectionState::TlsHandshaking;
  FlushResult res;
  {
    std::lock_guard<std::mutex> lock(transport_mutex_);
    if (role_ == Role::Client) {
      res = std::visit(
          [](auto &transport) {
            if constexpr (std::is_same_v<std::decay_t<decltype(transport)>,
                                         TLSTransport>) {
              return transport.connect();
            } else {
              return FlushResult{0, IOStatus::Complete};
            }
          },
          transport_);
    } else {
      res = std::visit(
          [](auto &transport) {
            if constexpr (std::is_same_v<std::decay_t<decltype(transport)>,
                                         TLSTransport>) {
              return transport.accept();
            } else {
              return FlushResult{0, IOStatus::Complete};
            }
          },
          transport_);
    }
  }
  if (res.status == IOStatus::WantWrite ||
      (res.status == IOStatus::Complete && has_to_write_.load())) {
    write_interest_handler_(true);
  } else {
    write_interest_handler_(false);
  }

  if (res.status == IOStatus::Complete) {
    state_.store(ConnectionState::Established);
  } else if (res.status == IOStatus::Error) {
    state_.store(ConnectionState::Closing);
  }
  return res;
}

std::expected<TCPConnection, TCPConnectionError>
TCPConnection::connect(Socket &&socket, const Endpoint &endpoint, bool use_tls,
                       Role role) {
  fcntl(socket.get_fd(), F_SETFL,
        fcntl(socket.get_fd(), F_GETFL, 0) | O_NONBLOCK);
  auto res = EndpointManager::Connect(socket, endpoint);
  if (res == -1) {
    if (errno != EINPROGRESS) {
      spdlog::error("error connecting to endpoint {}", (void *)&endpoint);
      return std::unexpected(TCPConnectionError::WrongProtocol);
    }
  }
  return TCPConnection{std::move(socket), endpoint, use_tls, role};
}

void TCPConnection::send(std::span<const std::byte> data) {
  auto framed = Framer::frame(data);
  spdlog::debug("sending {} bytes ({} framed) to connection {}", data.size(),
                framed.size(), (void *)this);
  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    send_buffer_.send(framed);
    has_to_write_.store(true);
    write_interest_handler_(true);
  }
}

bool TCPConnection::has_to_write() const { return has_to_write_.load(); }

FlushResult TCPConnection::flush() {
  FlushResult res{};
  {
    std::lock_guard<std::mutex> lock(write_mutex_);
    if (send_buffer_.empty()) {
      has_to_write_.store(false);
      write_interest_handler_(false);
      return {0, IOStatus::Complete};
    }

    {
      std::lock_guard<std::mutex> lock(transport_mutex_);
      res = send_buffer_.flush(transport_);
    }

    has_to_write_.store(!send_buffer_.empty());
    write_interest_handler_(!send_buffer_.empty());
  }
  return res;
}

std::expected<std::size_t, TCPConnectionError>
TCPConnection::recv(std::span<std::byte> data) {
  FlushResult res;
  {
    std::lock_guard<std::mutex> lock(transport_mutex_);
    res = std::visit(
        [&data](auto &transport) {
          auto res = transport.read(data);
          return res;
        },
        transport_);
  }
  if (res.status == IOStatus::Complete) {
    return res.bytes_sent;
  } else {
    return std::unexpected(TCPConnectionError::ReceiveError);
  }
}

void TCPConnection::set_write_interest_handler(
    const std::function<void(bool)> &func) {
  write_interest_handler_ = func;
}

IOStatus TCPConnection::receive() {
  std::lock_guard<std::mutex> lock(transport_mutex_);
  return receive_buffer_.consume(transport_);
}

std::optional<std::vector<std::byte>> TCPConnection::next_message() {
  return receive_buffer_.next_message();
}

TCPConnection::~TCPConnection() {
  spdlog::info("connection {} with fd={} closed", (void *)this,
               socket_.get_fd());
}

const Endpoint &TCPConnection::endpoint() const { return endpoint_; }

const Socket &TCPConnection::socket() const { return socket_; }

UDPConnection::UDPConnection(Socket &&socket, const Endpoint &endpoint)
    : socket_(std::move(socket)), endpoint_(endpoint) {}
