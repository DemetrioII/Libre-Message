#include "models/framer.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <gtest/gtest.h>
#include <string_view>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> result(text.size());
  std::memcpy(result.data(), text.data(), text.size());
  return result;
}

} // namespace

TEST(FramerTest, RoundTrip) {
  const auto payload = bytes("hello, framer");

  const auto framed = Framer::frame(payload);
  ASSERT_EQ(framed.size(), sizeof(std::uint32_t) + payload.size());

  const auto unframed = Framer::unframe(framed);
  ASSERT_TRUE(unframed.has_value());
  EXPECT_EQ(*unframed, payload);
}

TEST(FramerTest, EmptyPayload) {
  const std::vector<std::byte> payload;

  const auto framed = Framer::frame(payload);
  ASSERT_EQ(framed.size(), sizeof(std::uint32_t));

  const auto unframed = Framer::unframe(framed);
  ASSERT_TRUE(unframed.has_value());
  EXPECT_TRUE(unframed->empty());
}

TEST(FramerTest, ShortHeader) {
  const std::vector<std::byte> data = {std::byte{0x00}, std::byte{0x01},
                                       std::byte{0x02}};

  const auto unframed = Framer::unframe(data);
  ASSERT_FALSE(unframed.has_value());
  EXPECT_EQ(unframed.error(), FramerError::ShortHeader);
}

TEST(FramerTest, ShortPayload) {
  std::vector<std::byte> data(sizeof(std::uint32_t) + 3);
  const auto declared_length = htonl(5);
  std::memcpy(data.data(), &declared_length, sizeof(declared_length));

  const auto unframed = Framer::unframe(data);
  ASSERT_FALSE(unframed.has_value());
  EXPECT_EQ(unframed.error(), FramerError::ShortPayload);
}

TEST(FramerTest, FrameTooLarge) {
  std::vector<std::byte> data(sizeof(std::uint32_t));
  const auto declared_length =
      htonl(static_cast<std::uint32_t>(Framer::kMaxFrameSize + 1));
  std::memcpy(data.data(), &declared_length, sizeof(declared_length));

  const auto unframed = Framer::unframe(data);
  ASSERT_FALSE(unframed.has_value());
  EXPECT_EQ(unframed.error(), FramerError::FrameTooLarge);
}

TEST(FramerTest, NetworkByteOrder) {
  const auto payload = bytes("length-prefixed");

  const auto framed = Framer::frame(payload);
  ASSERT_GE(framed.size(), sizeof(std::uint32_t));

  std::uint32_t wire_length = 0;
  std::memcpy(&wire_length, framed.data(), sizeof(wire_length));

  EXPECT_EQ(wire_length, htonl(static_cast<std::uint32_t>(payload.size())));
}
