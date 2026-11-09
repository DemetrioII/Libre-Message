#include "models/framer.hpp"
#include <arpa/inet.h>
#include <cstring>

std::vector<std::byte> Framer::frame(std::span<const std::byte> payload) {
  std::vector<std::byte> result(sizeof(std::uint32_t) + payload.size());
  const auto length = htonl(static_cast<std::uint32_t>(payload.size()));

  std::memcpy(result.data(), &length, sizeof(length));
  std::memcpy(result.data() + sizeof(length), payload.data(), payload.size());

  return result;
}

std::expected<std::vector<std::byte>, FramerError>
Framer::unframe(std::span<const std::byte> data) {
  if (data.size() < sizeof(std::uint32_t))
    return std::unexpected(FramerError::ShortHeader);

  std::uint32_t length;
  std::memcpy(&length, data.data(), sizeof(length));
  length = ntohl(length);

  if (length > kMaxFrameSize)
    return std::unexpected(FramerError::FrameTooLarge);

  if (data.size() < sizeof(length) + length)
    return std::unexpected(FramerError::ShortPayload);

  std::vector<std::byte> payload(length);

  std::memcpy(payload.data(), data.data() + sizeof(length), length);

  return payload;
}