#include "net/transport/receivebuffer.hpp"
#include "models/framer.hpp"
#include <array>
#include <spdlog/spdlog.h>

void ReceiveBuffer::append(std::span<const std::byte> data) {
  large_.insert(large_.end(), data.begin(), data.end());
}

std::size_t ReceiveBuffer::size() const noexcept { return large_.size(); }

std::span<const std::byte> ReceiveBuffer::data() const noexcept {
  return large_;
}

void ReceiveBuffer::clear() noexcept { large_.clear(); }

IOStatus ReceiveBuffer::consume(TCPTransport &transport) {
  std::array<std::byte, 8192> chunk{};

  while (true) {
    const auto res = std::visit(
        [&chunk](auto &t) {
          return t.read(std::span(chunk.data(), chunk.size()));
        },
        transport);

    switch (res.status) {
    case IOStatus::Complete:
      append(std::span(chunk.data(), res.bytes_sent));
      if (res.bytes_sent == 0)
        return IOStatus::Complete;
      continue;

    case IOStatus::WantRead:
    case IOStatus::WantWrite:
      append(std::span(chunk.data(), res.bytes_sent));
      return IOStatus::Complete;

    case IOStatus::Closed:
      append(std::span(chunk.data(), res.bytes_sent));
      return IOStatus::Closed;

    case IOStatus::Error:
      return IOStatus::Error;
    }

    spdlog::info("received chunk: {}",
                 std::string(reinterpret_cast<const char *>(chunk.data()),
                             chunk.size()));
  }
}

std::optional<std::vector<std::byte>> ReceiveBuffer::next_message() {
  auto frame = Framer::unframe(large_);

  if (!frame) {
    switch (frame.error()) {
    case FramerError::ShortHeader:
    case FramerError::ShortPayload:
      return std::nullopt;

    case FramerError::FrameTooLarge:
      spdlog::error("received frame header exceeds limit ({} bytes), "
                    "dropping receive buffer",
                    Framer::kMaxFrameSize);
      clear();
      return std::nullopt;
    }
  }

  const auto &payload = *frame;
  const auto consumed = sizeof(std::uint32_t) + payload.size();
  large_.erase(large_.begin(), large_.begin() + consumed);

  return payload;
}
