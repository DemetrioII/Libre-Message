#include "models/message.hpp"
#include <cstring>

bool MessageModel::is_text() const { return message_.has_text(); }

std::string_view MessageModel::text() const { return message_.text().text(); }

bool MessageModel::is_for(const std::vector<std::byte> &user) const {
  const std::string &recipient = message_.recipient_id();
  if (recipient.size() != user.size())
    return false;
  return std::memcmp(recipient.data(), user.data(), user.size()) == 0;
}

bool MessageModel::is_handshake() const { return message_.has_handshake(); }

std::string_view MessageModel::handshake_sender_id() const {
  return message_.handshake().sender_id();
}

const libre_message::Message &MessageModel::proto() const { return message_; }
