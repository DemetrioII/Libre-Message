#include "models/framer.hpp"
#include "net/transport/receivebuffer.hpp"
#include "net/transport/sendbuffer.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

class TransportTest : public ::testing::Test {
protected:
  void SetUp() override { ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0); }

  void TearDown() override {
    if (fds_[0] != -1)
      close(fds_[0]);
    if (fds_[1] != -1)
      close(fds_[1]);
  }

  int fds_[2] = {-1, -1};
};

TEST_F(TransportTest, SingleFrame) {
  TCPTransport sender = PlainTCPTransport(fds_[0]);
  TCPTransport receiver = PlainTCPTransport(fds_[1]);

  const std::vector<std::byte> payload = {
      std::byte('h'), std::byte('e'), std::byte('l'), std::byte('l'),
      std::byte('o')};

  SendBuffer<> send_buf;
  send_buf.send(Framer::frame(payload));
  EXPECT_EQ(send_buf.flush(sender).status, IOStatus::Complete);

  ReceiveBuffer recv_buf;
  const auto status = recv_buf.consume(receiver);
  ASSERT_NE(status, IOStatus::Error);
  ASSERT_NE(status, IOStatus::Closed);

  const auto message = recv_buf.next_message();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message->size(), payload.size());
  EXPECT_EQ((*message)[0], std::byte('h'));
  EXPECT_EQ(recv_buf.size(), 0u);
}

TEST_F(TransportTest, TwoConsecutiveFrames) {
  TCPTransport sender = PlainTCPTransport(fds_[0]);
  TCPTransport receiver = PlainTCPTransport(fds_[1]);

  SendBuffer<> send_buf;
  ReceiveBuffer recv_buf;

  const std::vector<std::byte> first = {std::byte('a'), std::byte('b')};
  send_buf.send(Framer::frame(first));
  ASSERT_NE(send_buf.flush(sender).status, IOStatus::Error);
  ASSERT_NE(recv_buf.consume(receiver), IOStatus::Error);

  const auto first_msg = recv_buf.next_message();
  ASSERT_TRUE(first_msg.has_value());
  EXPECT_EQ(first_msg->size(), first.size());
  EXPECT_EQ((*first_msg)[0], std::byte('a'));

  const std::vector<std::byte> second = {std::byte('x')};
  send_buf.send(Framer::frame(second));
  ASSERT_NE(send_buf.flush(sender).status, IOStatus::Error);
  ASSERT_NE(recv_buf.consume(receiver), IOStatus::Error);

  const auto second_msg = recv_buf.next_message();
  ASSERT_TRUE(second_msg.has_value());
  EXPECT_EQ(second_msg->size(), 1u);
  EXPECT_EQ((*second_msg)[0], std::byte('x'));
  EXPECT_EQ(recv_buf.size(), 0u);
}