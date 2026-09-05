#pragma once
#include "app/connection_manager.hpp"
#include "app/session.hpp"
#include "models/message.hpp"
#include "models/proto/message.pb.h"
#include "net/network/peer.hpp"
#include <expected>

enum class ManagerError { SendError };

class Manager {
  SessionInfo session_info_;
  TCPPeer peer_net_;

  std::unordered_map<std::string, int> connections_;

public:
  Manager(std::string_view host, uint16_t port, int version, bool use_tls);

  std::expected<std::size_t, ManagerError> send(const libre_message::Message &);

  std::expected<void, ManagerError>
  connect(std::string_view host, uint16_t port,
          int version); // тут следует сделать конечно более абстракцию

  void send(const MessageModel &message);
};
