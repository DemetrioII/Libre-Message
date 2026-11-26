#pragma once
#include "app/connection_manager.hpp"
#include "app/contact_qr.hpp"
#include "app/session.hpp"
#include "models/message.hpp"
#include "models/proto/message.pb.h"
#include "net/network/peer.hpp"
#include <expected>
#include <mutex>

enum class ManagerError { ConnectFailed };

enum class ContactCheckResult {
  Match,
  UnknownContact,
  KeyMismatch,
};

class Manager {
  SessionInfo session_info_;
  ConnectionManager connection_manager_;
  TrustedContacts trusted_contacts_;
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

  std::expected<void, ContactError>
  export_contact(const std::filesystem::path &png) const;

  // Called only after the user has confirmed the displayed invitation.
  std::expected<void, ContactError>
  trust_contact(const ContactInvitation &invitation);

  std::expected<ContactInvitation, ContactError> own_contact() const;

  std::expected<ContactCheckResult, ContactError>
  check_contact(std::string_view user_id, std::string_view fingerprint) const;
};
