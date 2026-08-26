#pragma once
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

  std::unordered_map<int, std::vector<std::byte>> large_;

public:
  std::size_t send(int fd, std::span<const std::byte> data);

  std::expected<std::size_t, SendError> flush(int fd);
};
