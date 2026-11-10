#pragma once
#include "app/connection_manager.hpp"
#include "app/session.hpp"
#include "models/message.hpp"
#include "models/proto/message.pb.h"
#include "net/network/peer.hpp"
#include <expected>
#include <mutex>

enum class ManagerError { ConnectFailed };

class Manager {
  SessionInfo session_info_;
  ConnectionManager connection_manager_;
  TCPPeer peer_net_;
  std::string sender_id_;

  std::mutex send_mutex_;

public:
  Manager(std::string_view host, uint16_t port, int version, bool use_tls);

  std::expected<void, ManagerError>
  connect(std::string_view host, uint16_t port,
          int version); // тут следует сделать конечно более абстракцию

  void send(const MessageModel &message);

  void send(std::string_view, std::string &&msg_to_send);

  void run_event_loop();
};
