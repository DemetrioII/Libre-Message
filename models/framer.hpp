#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <vector>

enum class FramerError {
  ShortHeader,
  ShortPayload,
  FrameTooLarge,
};

class Framer {

public:
  static constexpr std::size_t kMaxFrameSize = 64 * 1024 * 1024;

  static std::vector<std::byte> frame(std::span<const std::byte> payload);

  static std::expected<std::vector<std::byte>, FramerError>
  unframe(std::span<const std::byte> data);
};