#include "net/transport/receivebuffer.hpp"

void ReceiveBuffer::append(std::span<const std::byte> data) {
  large_.insert(large_.end(), data.begin(), data.end());
}

std::optional<std::vector<std::byte>> ReceiveBuffer::next_message() {}
