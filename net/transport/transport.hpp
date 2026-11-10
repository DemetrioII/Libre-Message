#pragma once
#include <expected>
#include <openssl/ssl.h>
#include <span>
#include <sys/socket.h>
#include <variant>
#include <vector>

enum class IOStatus {
  Complete,
  WantRead,
  WantWrite,
  Closed,
  Error,
};

struct FlushResult {
  std::size_t bytes_sent;
  IOStatus status;
};

enum class ConnectionState { Connecting, TlsHandshaking, Established, Closing };
enum class TCPTransportError {
  UnknownError,
};

class PlainTCPTransport {
  int fd_;

public:
  PlainTCPTransport(int fd) : fd_(fd) {}

  PlainTCPTransport(const PlainTCPTransport &other) = delete;
  PlainTCPTransport &operator=(const PlainTCPTransport &) = delete;

  PlainTCPTransport(PlainTCPTransport &&) = default;
  PlainTCPTransport &operator=(PlainTCPTransport &&) = default;
  FlushResult write(std::span<const std::byte> data);
  FlushResult read(std::span<std::byte> data);

  ~PlainTCPTransport();
};

enum class TLSState {
  Handshaking,
  Established,
  Closed,
  Error,
};

class TLSTransport {
  SSL *ssl_ = nullptr;
  SSL_CTX *ctx_ = nullptr;
  int fd_;

  TLSState state_;

public:
  TLSTransport(int fd);
  TLSState state() const noexcept;

  void set_role(int is_client = 0);

  FlushResult connect();
  FlushResult accept();
  FlushResult write(std::span<const std::byte> data);
  FlushResult read(std::span<std::byte> data);

  TLSTransport(const TLSTransport &) = delete;
  TLSTransport &operator=(const TLSTransport &) = delete;

  TLSTransport(TLSTransport &&other) noexcept;
  TLSTransport &operator=(TLSTransport &&other) noexcept;
  ~TLSTransport();
};

using TCPTransport = std::variant<PlainTCPTransport, TLSTransport>;
