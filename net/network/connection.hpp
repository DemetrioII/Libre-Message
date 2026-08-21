#pragma once
#include "net/network/endpoint.hpp"
#include "net/network/rawsocket.hpp"
#include <expected>
#include <fcntl.h>
#include <span>
#include <sys/socket.h>
#include <unistd.h>

enum class TCPConnectionError {
  WrongProtocol,
};

class TCPConnection {
  Socket socket_;

public:
  TCPConnection(Socket &&socket);

  static std::expected<TCPConnection, TCPConnectionError>
  connect(const Socket &socket, const Endpoint &endpoint);

  std::expected<std::size_t, TCPConnectionError>
  send(std::span<const std::byte> data);

  std::expected<std::size_t, TCPConnectionError>
  recv(std::span<std::byte> data);
};
