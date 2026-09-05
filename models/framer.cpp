#include "models/framer.hpp"
#include <cstdint>

std::vector<std::byte> Framer::frame(std::span<const std::byte> data) {
  std::vector<std::byte> result(sizeof(std::uint32_t) + data.size());
  const auto size = static_cast<std::uint32_t>(data.size());

  std::memcpy(result.data(), &size, sizeof(size));
  std::memcpy(result.data() + sizeof(size), data.data(), data.size());

  return result;
}

std::expected<std::vector<std::byte>, FramerError>
unframe(std::span<const std::byte> data) {
  if (data.size() < sizeof(std::uint32_t))
    return std::unexpected(FramerError::ShortHeader);

  std::uint32_t size;

  std::memcpy(&size, data.data(), sizeof(size));

  if (data.size() < sizeof(size) + size)
    return std::unexpected(FramerError::ShortPayload);

  std::vector<std::byte> payload(size);

  std::memcpy(payload.data(), data.data() + sizeof(size), size);

  return payload;
}
