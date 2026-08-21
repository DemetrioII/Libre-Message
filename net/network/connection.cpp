#include "net/network/connection.hpp"

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

TCPConnection::TCPConnection(Socket &&socket) : socket_(std::move(socket)) {}

std::expected<TCPConnection, TCPConnectionError>
TCPConnection::connect(const Socket &socket, const Endpoint &endpoint) {
  auto res = EndpointManager::Connect(socket, endpoint);
  if (res == -1)
    return std::unexpected(TCPConnectionError::WrongProtocol);
  return TCPConnection{Socket{res}};
}

std::expected<std::size_t, TCPConnectionError>
TCPConnection::send(std::span<const std::byte> data) {
  return 0;
}

std::expected<std::size_t, TCPConnectionError>
TCPConnection::recv(std::span<std::byte> data) {
  return 0;
}
