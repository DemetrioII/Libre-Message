#pragma once
#include "net/transport/transport.hpp"
#include <optional>
#include <span>
#include <vector>

class ReceiveBuffer {
  std::vector<std::byte> large_;

public:
  void append(std::span<const std::byte> data);

  std::size_t size() const noexcept;

  std::span<const std::byte> data() const noexcept;

  void clear() noexcept;

  IOStatus consume(TCPTransport &transport);

  std::optional<std::vector<std::byte>> next_message();
};