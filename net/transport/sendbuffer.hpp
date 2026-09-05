#pragma once
#include "net/transport/transport.hpp"
#include <array>
#include <expected>
#include <span>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <variant>
#include <vector>

enum class SendError {
  WouldBlock,
  SignalInterrupt,
  UnknownError,
};

template <std::size_t N = 1024> class SendBuffer {
  std::array<std::byte, N> small_;

  std::vector<std::byte> large_;

public:
  void send(std::span<const std::byte> data);

  FlushResult flush(TCPTransport &transport);
};
