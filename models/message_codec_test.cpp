#include "models/message_codec.hpp"

#include <gtest/gtest.h>

namespace {

libre_message::Message make_message() {
  libre_message::Message message;
  message.set_id("msg-1");
  message.set_sender_id("user-1");
  message.set_recipient_id("user-2");
  message.set_timestamp(123456789);
  message.mutable_text()->set_text("hello, world");
  return message;
}

} // namespace

TEST(MessageCodecTest, RoundTrip) {
  const MessageModel model(make_message());
  const auto data = MessageCodec::encode(model);

  ASSERT_TRUE(data.size() > 0);

  const auto decoded = MessageCodec::decode(data);
  ASSERT_TRUE(decoded.has_value());

  EXPECT_EQ(decoded->proto().id(), "msg-1");
  EXPECT_EQ(decoded->proto().sender_id(), "user-1");
  EXPECT_EQ(decoded->proto().recipient_id(), "user-2");
  EXPECT_EQ(decoded->proto().timestamp(), 123456789);
  EXPECT_TRUE(decoded->is_text());
  EXPECT_EQ(decoded->text(), "hello, world");
}

TEST(MessageCodecTest, InvalidData) {
  const std::vector<std::byte> garbage = {
      std::byte{0x0b}, std::byte{0x10}, std::byte{0x00}, std::byte{0x12}};

  const auto decoded = MessageCodec::decode(garbage);
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), DecodeError::InvalidData);
}

TEST(MessageCodecTest, MissingMessage) {
  libre_message::Envelope envelope;
  const auto size = envelope.ByteSizeLong();
  std::vector<std::byte> data(size);
  envelope.SerializeToArray(data.data(), size);

  const auto decoded = MessageCodec::decode(data);
  ASSERT_FALSE(decoded.has_value());
  EXPECT_EQ(decoded.error(), DecodeError::MissingMessage);
}