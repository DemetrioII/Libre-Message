#pragma once
#include <optional>
#include <span>
#include <vector>

class ReceiveBuffer {
  std::vector<std::byte> large_;

public:
  void append(std::span<const std::byte> data);

  std::optional<std::vector<std::byte>> next_message();
};
