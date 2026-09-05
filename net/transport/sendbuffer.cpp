#include "net/transport/sendbuffer.hpp"
#include <errno.h>
#include <spdlog/spdlog.h>

template <> void SendBuffer<>::send(std::span<const std::byte> data) {
  large_.insert(large_.end(), data.begin(), data.end());
}

template <> FlushResult SendBuffer<>::flush(TCPTransport &transport) {
  const auto res = std::visit(
      [this](auto &transport) {
        const auto res =
            transport.write(std::span(large_.begin(), large_.end()));
        return res;
      },
      transport);

  if (res.status != IOStatus::Error) {
    large_.erase(large_.begin(), large_.begin() + res.bytes_sent);
  } else {
    spdlog::error("error sending using transport {}", (void *)&transport);
  }

  return res;
}
