#pragma once
#include "models/message.hpp"
#include <expected>
#include <span>

enum class DecodeError {
  InvalidData,
  MissingMessage,
};

class MessageCodec {
public:
  static std::vector<std::byte> encode(const MessageModel &message);
  static std::expected<MessageModel, DecodeError>
  decode(std::span<const std::byte> data);
};
