#include "models/message.hpp"

bool MessageModel::is_text() const { return message_.has_text(); }

std::string_view MessageModel::text() const { return message_.text().text(); }

const libre_message::Message &MessageModel::proto() const { return message_; }
