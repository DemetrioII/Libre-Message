#include "app/connection_manager.hpp"
#include <spdlog/spdlog.h>

void ConnectionManager::register_connection(std::string recipient_id,
                                            ConnectionId id) {
  std::lock_guard<std::mutex> lock{mutex_};
  recipient_to_id_[std::move(recipient_id)] = id;
}

std::optional<ConnectionId>
ConnectionManager::find_id(std::string_view recipient_id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  auto it = recipient_to_id_.find(std::string{recipient_id});
  if (it == recipient_to_id_.end())
    return std::nullopt;
  return it->second;
}

void ConnectionManager::unregister(std::string_view recipient_id) {
  std::lock_guard<std::mutex> lock{mutex_};
  auto it = recipient_to_id_.find(std::string{recipient_id});
  if (it == recipient_to_id_.end()) {
    spdlog::warn("recipient_id={} not found in connection manager",
                 recipient_id);
    return;
  }
  recipient_to_id_.erase(it);
}

void ConnectionManager::unregister_by_id(ConnectionId id) {
  std::lock_guard<std::mutex> lock{mutex_};
  std::erase_if(recipient_to_id_,
                [id](const auto &entry) { return entry.second == id; });
}
