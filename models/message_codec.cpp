#include "models/message_codec.hpp"

std::vector<std::byte> MessageCodec::encode(const MessageModel &message) {
  libre_message::Envelope envelope;

  envelope.mutable_message()->set_id(message.proto().id().data(),
                                     message.proto().id().size());

  envelope.mutable_message()->set_sender_id(message.proto().sender_id().data(),
                                            message.proto().sender_id().size());

  envelope.mutable_message()->set_timestamp(message.proto().timestamp());

  envelope.mutable_message()->mutable_text()->set_text(message.text());

  const auto size = envelope.ByteSizeLong();

  std::vector<std::byte> result(size);

  envelope.SerializeToArray(result.data(), size);

  return result;
}

std::expected<MessageModel, DecodeError>
MessageCodec::decode(std::span<const std::byte> data) {}
