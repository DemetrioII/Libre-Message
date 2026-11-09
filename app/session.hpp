#pragma once

#include <cstddef>
#include <vector>

struct SessionInfo {
  static constexpr std::size_t kSessionIdSize = 16;

  std::vector<std::byte> session_id;

  static SessionInfo generate();
};