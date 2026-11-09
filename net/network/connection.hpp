#pragma once
#include "net/network/endpoint.hpp"
#include "net/network/rawsocket.hpp"
#include "net/transport/receivebuffer.hpp"
#include "net/transport/sendbuffer.hpp"
#include <expected>
#include <fcntl.h>
#include <optional>
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

  ReceiveBuffer receive_buffer_;

  TCPTransport transport_;

  bool has_to_write_{false};

public:
  TCPConnection() : socket_(-1), transport_(PlainTCPTransport(-1)) {}
  TCPConnection(Socket &&socket, const Endpoint &endpoint, bool use_tls);

  TCPConnection(TCPConnection &&) = default;

  TCPConnection &operator=(TCPConnection &&) = default;

  static std::expected<TCPConnection, TCPConnectionError>
  connect(Socket &&socket, const Endpoint &endpoint, bool use_tls);

  void send(std::span<const std::byte> data);

  std::expected<std::size_t, TCPConnectionError>
  recv(std::span<std::byte> data);

  IOStatus receive();

  std::optional<std::vector<std::byte>> next_message();

  bool has_to_write() const;

  FlushResult flush();

  const Socket &socket() const;

  void set_tls_connection();

  void set_tls_acception();

  const Endpoint &endpoint() const;

  ~TCPConnection();
};

class UDPConnection {
  Socket socket_;
  Endpoint endpoint_;

public:
  UDPConnection(Socket &&socket, const Endpoint &endpoint);
};
