#include "models/message_codec.hpp"
#include <utility>

std::vector<std::byte> MessageCodec::encode(const MessageModel &message) {
  libre_message::Envelope envelope;

  envelope.mutable_message()->set_id(message.proto().id().data(),
                                     message.proto().id().size());

  envelope.mutable_message()->set_sender_id(message.proto().sender_id().data(),
                                            message.proto().sender_id().size());

  envelope.mutable_message()->set_recipient_id(
      message.proto().recipient_id().data(),
      message.proto().recipient_id().size());

  envelope.mutable_message()->set_timestamp(message.proto().timestamp());

  if (message.is_handshake()) {
    envelope.mutable_message()->mutable_handshake()->set_sender_id(
        message.handshake_sender_id());
  } else {
    envelope.mutable_message()->mutable_text()->set_text(message.text());
  }

  const auto size = envelope.ByteSizeLong();

  std::vector<std::byte> result(size);

  envelope.SerializeToArray(result.data(), size);

  return result;
}

std::expected<MessageModel, DecodeError>
MessageCodec::decode(std::span<const std::byte> data) {
  libre_message::Envelope envelope;

  if (!envelope.ParseFromArray(data.data(), static_cast<int>(data.size())))
    return std::unexpected(DecodeError::InvalidData);

  if (!envelope.has_message())
    return std::unexpected(DecodeError::MissingMessage);

  return MessageModel(std::move(*envelope.mutable_message()));
}
