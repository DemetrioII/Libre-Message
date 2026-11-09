#pragma once

#include <expected>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

enum class ConnectionError {
  AlreadyRegistered,
  NotFound,
};

class ConnectionManager {
  mutable std::mutex mutex_;
  std::unordered_map<std::string, int> recipient_to_fd_;

public:
  void register_connection(std::string recipient_id, int fd);

  std::optional<int> find_fd(std::string_view recipient_id) const;

  void unregister(std::string_view recipient_id);

  void unregister_by_fd(int fd);
};
