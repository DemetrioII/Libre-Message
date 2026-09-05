#pragma once
#include "models/proto/message.pb.h"

class MessageModel {
  libre_message::Message message_;

public:
  explicit MessageModel(libre_message::Message message)
      : message_(std::move(message)) {}

  bool is_text() const;

  std::string_view text() const;

  bool is_for(const std::vector<std::byte> &user) const;

  const libre_message::Message &proto() const;
};
