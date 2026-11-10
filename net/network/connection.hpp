#pragma once
#include "net/network/endpoint.hpp"
#include "net/network/rawsocket.hpp"
#include "net/transport/receivebuffer.hpp"
#include "net/transport/sendbuffer.hpp"
#include "net/transport/transport.hpp"
#include <atomic>
#include <cstdint>
#include <expected>
#include <fcntl.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sys/socket.h>
#include <unistd.h>

enum class TCPConnectionError {
  WrongProtocol,
  SendError,
  ReceiveError,
};

enum class Role {
  Client,
  Server,
};

class TCPConnection {
  Socket socket_;
  Endpoint endpoint_;
  Role role_;

  std::mutex write_mutex_;
  std::mutex transport_mutex_;

  SendBuffer<> send_buffer_;

  ReceiveBuffer receive_buffer_;

  TCPTransport transport_;
  ConnectionState state_{ConnectionState::Connecting};

  std::function<void(bool)> write_interest_handler_;
  std::atomic<bool> has_to_write_{false};

public:
  TCPConnection() : socket_(-1), transport_(PlainTCPTransport(-1)) {}
  TCPConnection(Socket &&socket, const Endpoint &endpoint, bool use_tls,
                Role role);

  TCPConnection(TCPConnection &&other) noexcept;

  TCPConnection &operator=(TCPConnection &&other) noexcept;

  static std::expected<TCPConnection, TCPConnectionError>
  connect(Socket &&socket, const Endpoint &endpoint, bool use_tls, Role role);

  void send(std::span<const std::byte> data);

  std::expected<std::size_t, TCPConnectionError>
  recv(std::span<std::byte> data);

  void set_write_interest_handler(const std::function<void(bool)> &func);

  IOStatus receive();

  std::optional<std::vector<std::byte>> next_message();

  ConnectionState state();

  FlushResult continue_handshake();

  FlushResult handshake_step();

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

using ConnectionId = std::uint64_t;

inline constexpr ConnectionId kInvalidConnectionId = 0;

struct ConnectionEntry {
  ConnectionId id;
  std::shared_ptr<TCPConnection> connection;
};
