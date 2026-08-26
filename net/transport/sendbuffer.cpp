#include "net/transport/sendbuffer.hpp"
#include <errno.h>

template <>
std::size_t SendBuffer<>::send(int fd, std::span<const std::byte> data) {
  if (large_.find(fd) != large_.end()) {
    large_[fd].insert(large_[fd].end(), data.begin(), data.end());
  } else {
    large_[fd] = std::vector<std::byte>{data.begin(), data.end()};
  }

  return data.size();
}

template <> std::expected<std::size_t, SendError> SendBuffer<>::flush(int fd) {
  std::size_t total = 0;
  const auto &data = large_[fd];
  while (total < data.size()) {
    const auto n = ::send(fd, data.data() + total, data.size() - total, 0);

    if (n > 0) {
      total += static_cast<std::size_t>(n);
      continue;
    }
    if (n == 0) {
      return std::unexpected(SendError::UnknownError);
    }

    if (errno == EINTR) {
      continue;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return std::unexpected(SendError::WouldBlock);
    }

    return 0;
  }
  large_[fd].erase(large_[fd].begin(), large_[fd].begin() + total);
  return total;
}
