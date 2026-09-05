#pragma once

#include <cstring>
#include <expected>
#include <span>
#include <stdexcept>
#include <vector>

enum class FramerError {
  ShortHeader,
  ShortPayload,
};

class Framer {

public:
  static std::vector<std::byte> frame(std::span<const std::byte> payload);

  static std::expected<std::vector<std::byte>, FramerError>
  unframe(std::span<const std::byte> data);
};
