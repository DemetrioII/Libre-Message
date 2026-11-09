#include "app/connection_manager.hpp"

void ConnectionManager::register_connection(std::string recipient_id, int fd) {
  std::lock_guard<std::mutex> lock{mutex_};
  recipient_to_fd_[std::move(recipient_id)] = fd;
}

std::optional<int>
ConnectionManager::find_fd(std::string_view recipient_id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  auto it = recipient_to_fd_.find(std::string{recipient_id});
  if (it == recipient_to_fd_.end())
    return std::nullopt;
  return it->second;
}

void ConnectionManager::unregister(std::string_view recipient_id) {
  std::lock_guard<std::mutex> lock{mutex_};
  recipient_to_fd_.erase(std::string{recipient_id});
}

void ConnectionManager::unregister_by_fd(int fd) {
  std::lock_guard<std::mutex> lock{mutex_};
  std::erase_if(recipient_to_fd_,
                [fd](const auto &entry) { return entry.second == fd; });
}
