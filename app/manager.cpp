#include "app/manager.hpp"
#include "models/message_codec.hpp"
#include <expected>

Manager::Manager(std::string_view host, uint16_t port, int version,
                 bool use_tls)
    : peer_net_(host, port, version, use_tls) {}

std::expected<void, ManagerError> Manager::connect(std::string_view host,
                                                   uint16_t port, int version) {
  int fd = peer_net_.connect(host, port, version);
  return {};
}

void Manager::send(const MessageModel &message) {
  auto data = MessageCodec::encode(message);
  peer_net_.send_data(connections_[message.proto().recipient_id()], data);
}
