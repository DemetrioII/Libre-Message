#include "net/network/connection.hpp"
#include <errno.h>
#include <iostream>
#include <spdlog/spdlog.h>

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

TCPConnection::TCPConnection(Socket &&socket, const Endpoint &endpoint)
    : socket_(std::move(socket)), endpoint_(endpoint) {}

std::expected<TCPConnection, TCPConnectionError>
TCPConnection::connect(Socket &&socket, const Endpoint &endpoint) {
  auto res = EndpointManager::Connect(socket, endpoint);
  if (res == -1) {
    return std::unexpected(TCPConnectionError::WrongProtocol);
  }
  fcntl(socket.get_fd(), F_SETFL,
        fcntl(socket.get_fd(), F_GETFL, 0) | O_NONBLOCK);
  return TCPConnection{std::move(socket), endpoint};
}

std::expected<std::size_t, TCPConnectionError>
TCPConnection::send(std::span<const std::byte> data) {
  const auto res = send_buffer_.send(socket_.get_fd(), data);
  spdlog::debug("sending {} bytes to buffer on connection {}", data.size(),
                (void *)this);
  has_to_write_ = true;
  return res;
}

bool TCPConnection::has_to_write() const { return has_to_write_; }

std::expected<void, TCPConnectionError> TCPConnection::flush() {
  const auto res = send_buffer_.flush(socket_.get_fd());
  if (res.has_value())
    return {};
  return std::unexpected(TCPConnectionError::SendError);
}

std::expected<std::size_t, TCPConnectionError>
TCPConnection::recv(std::span<std::byte> data) {
  std::size_t total = 0;
  while (total < data.size()) {
    const ssize_t n =
        ::recv(socket_.get_fd(), data.data() + total, data.size() - total, 0);
    if (n > 0) {
      total += static_cast<std::size_t>(n);
      continue;
    }

    if (n == 0) {
      break;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }

    if (errno == EINTR) {
      continue;
    }

    spdlog::error("error during receiving on TCP connection {}, error: {}",
                  (void *)this, strerror(errno));

    return std::unexpected(TCPConnectionError::ReceiveError);
  }
  return total;
}

TCPConnection::~TCPConnection() {
  spdlog::info("connection {} with fd={} closed", (void *)this,
               socket_.get_fd());
}

const Endpoint &TCPConnection::endpoint() const { return endpoint_; }

const Socket &TCPConnection::socket() const { return socket_; }

UDPConnection::UDPConnection(Socket &&socket, const Endpoint &endpoint)
    : socket_(std::move(socket)), endpoint_(endpoint) {}
