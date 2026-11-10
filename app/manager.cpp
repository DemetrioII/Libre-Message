#include "app/manager.hpp"
#include "models/message_codec.hpp"
#include <expected>
#include <fstream>
#include <spdlog/spdlog.h>
#include <vector>

static constexpr const char *kUserIdFile = "user_id";

static bool is_valid_user_id(const std::vector<std::byte> &id) {
  for (std::byte b : id) {
    const char c = static_cast<char>(b);
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  }
  return true;
}

static SessionInfo load_or_create_session() {
  std::ifstream in(kUserIdFile, std::ios::binary);
  if (in) {
    std::vector<std::byte> id(SessionInfo::kSessionIdSize);
    in.read(reinterpret_cast<char *>(id.data()),
            static_cast<std::streamsize>(id.size()));
    if (in.gcount() == static_cast<std::streamsize>(id.size()) &&
        is_valid_user_id(id)) {
      spdlog::info("loaded user id from {}", kUserIdFile);
      return SessionInfo{std::move(id)};
    }
  }

  SessionInfo session = SessionInfo::generate();
  std::ofstream out(kUserIdFile, std::ios::binary | std::ios::trunc);
  if (out)
    out.write(reinterpret_cast<const char *>(session.session_id.data()),
              session.session_id.size());
  spdlog::info("generated new user id in {}", kUserIdFile);
  return session;
}

Manager::Manager(std::string_view host, uint16_t port, int version,
                 bool use_tls)
    : session_info_(load_or_create_session()),
      peer_net_(host, port, version, use_tls) {
  for (std::byte b : session_info_.session_id)
    sender_id_.push_back(static_cast<char>(b));

  peer_net_.set_sender_id(sender_id_);
  peer_net_.set_disconnect_handler([this](ConnectionId id) {
    connection_manager_.unregister_by_id(id);
    spdlog::info("removed recipient mapping for connection_id={}", id);
  });
  peer_net_.set_message_handler([this](ConnectionId id, MessageModel message) {
    if (message.is_handshake()) {
      connection_manager_.register_connection(
          std::string{message.handshake_sender_id()}, id);
      spdlog::info("registered peer with id={} on connection_id={}",
                   message.handshake_sender_id(), id);
      return;
    }
    if (message.is_for(session_info_.session_id))
      spdlog::info("message from {} for me: {}", message.proto().sender_id(),
                   message.text());
    else
      spdlog::warn("dropping message not addressed to me");
  });
}

std::expected<void, ManagerError> Manager::connect(std::string_view host,
                                                   uint16_t port, int version) {
  ConnectionId id = peer_net_.connect(host, port, version);
  if (id == kInvalidConnectionId)
    return std::unexpected(ManagerError::ConnectFailed);

  return {};
}

void Manager::send(const MessageModel &message) {

  auto id = connection_manager_.find_id(message.proto().recipient_id());
  if (!id) {
    spdlog::warn("no connection registered for recipient");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    auto data = MessageCodec::encode(message);
    peer_net_.send_data(*id, data);
  }
}

void Manager::run_event_loop() { peer_net_.run_event_loop(); }

void Manager::send(std::string_view recipient_id, std::string &&msg_to_send) {
  libre_message::Message message;
  message.set_id("demo");
  message.set_sender_id(
      reinterpret_cast<const char *>(session_info_.session_id.data()),
      session_info_.session_id.size());
  message.set_recipient_id(recipient_id.data(), recipient_id.size());
  message.mutable_text()->set_text(msg_to_send);
  message.set_timestamp(0);
  send(MessageModel(std::move(message)));
}
