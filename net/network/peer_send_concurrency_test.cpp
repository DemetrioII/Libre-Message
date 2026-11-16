#include "models/message.hpp"
#include "models/message_codec.hpp"
#include "net/network/peer.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <latch>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;

constexpr std::size_t kSenderCount = 4;
constexpr std::size_t kMessagesPerSender = 100;
constexpr std::size_t kTotalMessages = kSenderCount * kMessagesPerSender;

class ScopedFd {
public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() {
    if (fd_ >= 0)
      ::close(fd_);
  }

  ScopedFd(const ScopedFd &) = delete;
  ScopedFd &operator=(const ScopedFd &) = delete;

  int get() const { return fd_; }

private:
  int fd_;
};

int make_silent_listener(std::uint16_t &port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;

  if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    ::close(fd);
    return -1;
  }
  if (::listen(fd, 1) != 0) {
    ::close(fd);
    return -1;
  }

  socklen_t length = sizeof(address);
  if (::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
    ::close(fd);
    return -1;
  }

  port = ntohs(address.sin_port);
  return fd;
}

std::uint16_t reserve_port() {
  std::uint16_t port = 0;
  ScopedFd listener(make_silent_listener(port));
  return listener.get() >= 0 ? port : 0;
}

std::string make_message_id(std::size_t sender, std::size_t index) {
  return "message-" + std::to_string(sender) + "-" + std::to_string(index);
}

std::set<std::string> make_expected_ids() {
  std::set<std::string> ids;
  for (std::size_t sender = 0; sender < kSenderCount; ++sender) {
    for (std::size_t index = 0; index < kMessagesPerSender; ++index)
      ids.insert(make_message_id(sender, index));
  }
  return ids;
}

TEST(TCPPeerSendConcurrencyTest, ConcurrentSendDataPreservesMessages) {
  // Shared state is declared before the peers so that it outlives the event
  // loop threads that capture references to it.
  std::mutex mutex;
  std::set<std::string> received_ids;
  std::size_t text_message_count = 0;
  std::promise<void> all_received;
  auto all_received_future = all_received.get_future();
  std::atomic<bool> all_received_fulfilled{false};
  std::atomic<bool> receiver_disconnected{false};

  const auto port = reserve_port();
  ASSERT_NE(port, 0);

  TCPPeer server("127.0.0.1", port, 4, false);
  TCPPeer client("127.0.0.1", 0, 4, false);

  server.set_message_handler([&](ConnectionId, MessageModel message) {
    if (!message.is_text())
      return;

    std::lock_guard<std::mutex> lock(mutex);
    received_ids.insert(std::string(message.proto().id()));
    ++text_message_count;

    if (received_ids.size() == kTotalMessages &&
        !all_received_fulfilled.exchange(true))
      all_received.set_value();
  });

  server.set_disconnect_handler(
      [&](ConnectionId) { receiver_disconnected.store(true); });

  server.run_event_loop();
  client.run_event_loop();

  const auto id = client.connect("127.0.0.1", port, 4);
  ASSERT_NE(id, kInvalidConnectionId);

  std::latch start{1};
  std::vector<std::thread> senders;
  senders.reserve(kSenderCount);
  for (std::size_t sender = 0; sender < kSenderCount; ++sender) {
    senders.emplace_back([&, sender] {
      start.wait();
      for (std::size_t index = 0; index < kMessagesPerSender; ++index) {
        const auto message_id = make_message_id(sender, index);

        libre_message::Message message;
        message.set_id(message_id);
        message.set_sender_id("sender-" + std::to_string(sender));
        message.set_recipient_id("recipient");
        message.set_timestamp(static_cast<std::uint64_t>(
            sender * kMessagesPerSender + index + 1));
        message.mutable_text()->set_text("text-" + message_id);

        auto encoded =
            MessageCodec::encode(MessageModel(std::move(message)));
        client.send_data(id, encoded);
      }
    });
  }

  start.count_down();

  const auto status = all_received_future.wait_for(10s);

  for (auto &sender : senders)
    sender.join();

  ASSERT_EQ(status, std::future_status::ready);

  std::set<std::string> received;
  std::size_t text_count = 0;
  {
    std::lock_guard<std::mutex> lock(mutex);
    received = received_ids;
    text_count = text_message_count;
  }

  EXPECT_FALSE(receiver_disconnected.load());
  EXPECT_EQ(text_count, kTotalMessages);
  EXPECT_EQ(received, make_expected_ids());
  EXPECT_EQ(received.size(), kTotalMessages);
}

} // namespace
