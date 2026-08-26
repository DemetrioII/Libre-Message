#pragma once
#include "net/network/endpoint.hpp"
#include "net/network/rawsocket.hpp"
#include "net/transport/sendbuffer.hpp"
#include <expected>
#include <fcntl.h>
#include <span>
#include <sys/socket.h>
#include <unistd.h>

enum class TCPConnectionError {
  WrongProtocol,
  SendError,
  ReceiveError,
};

class TCPConnection {
  Socket socket_;
  Endpoint endpoint_;

  SendBuffer<> send_buffer_;

  bool has_to_write_{false};

public:
  TCPConnection() : socket_(-1) {}
  TCPConnection(Socket &&socket, const Endpoint &endpoint);

  TCPConnection(TCPConnection &&) = default;

  TCPConnection &operator=(TCPConnection &&) = default;

  static std::expected<TCPConnection, TCPConnectionError>
  connect(Socket &&socket, const Endpoint &endpoint);

  std::expected<std::size_t, TCPConnectionError>
  send(std::span<const std::byte> data);

  std::expected<std::size_t, TCPConnectionError>
  recv(std::span<std::byte> data);

  bool has_to_write() const;

  std::expected<void, TCPConnectionError> flush();

  const Socket &socket() const;

  const Endpoint &endpoint() const;

  ~TCPConnection();
};

class UDPConnection {
  Socket socket_;
  Endpoint endpoint_;

public:
  UDPConnection(Socket &&socket, const Endpoint &endpoint);
};
