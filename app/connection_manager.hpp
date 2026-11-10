#pragma once

#include "net/network/connection.hpp"
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
  std::unordered_map<std::string, ConnectionId> recipient_to_id_;

public:
  void register_connection(std::string recipient_id, ConnectionId id);

  std::optional<ConnectionId> find_id(std::string_view recipient_id) const;

  void unregister(std::string_view recipient_id);

  void unregister_by_id(ConnectionId id);
};
