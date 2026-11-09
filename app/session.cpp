#include "app/session.hpp"
#include <cstdint>
#include <random>

SessionInfo SessionInfo::generate() {
  static constexpr char kHexDigits[] = "0123456789abcdef";

  std::vector<std::byte> id(kSessionIdSize);
  std::random_device rd;
  std::uniform_int_distribution<unsigned int> dist{0, 15};
  for (std::size_t i = 0; i < kSessionIdSize; ++i)
    id[i] = static_cast<std::byte>(kHexDigits[dist(rd)]);
  return SessionInfo{std::move(id)};
}