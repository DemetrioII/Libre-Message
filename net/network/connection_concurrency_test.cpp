#include "net/network/connection.hpp"
#include "net/network/endpoint.hpp"
#include "net/network/rawsocket.hpp"
#include "net/transport/receivebuffer.hpp"
#include "net/transport/transport.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <latch>
#include <sys/socket.h>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kPayloadCount = 500;
constexpr std::size_t kPayloadSize = 64;
// Avoid filling the AF_UNIX skb accounting with many tiny writes before the
// receiver is drained after the threads join.
constexpr std::size_t kFlushBatchFrames = 16;

std::vector<std::vector<std::byte>> make_payloads() {
  std::vector<std::vector<std::byte>> payloads;
  payloads.reserve(kPayloadCount);
  for (std::size_t i = 0; i < kPayloadCount; ++i) {
    std::vector<std::byte> payload(kPayloadSize);
    payload[0] = std::byte(i & 0xFF);
    payload[1] = std::byte((i >> 8) & 0xFF);
    for (std::size_t j = 2; j < kPayloadSize; ++j) {
      payload[j] = std::byte((i * 31 + j * 17) & 0xFF);
    }
    payloads.push_back(std::move(payload));
  }
  return payloads;
}

TEST(ConnectionConcurrencyTest, ConcurrentSendAndFlushPreservesFrames) {
  int fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  Socket client_socket(fds[0]);
  Socket receiver_socket(fds[1]);

  for (const int fd : {client_socket.get_fd(), receiver_socket.get_fd()}) {
    const int flags = fcntl(fd, F_GETFL, 0);
    ASSERT_NE(flags, -1);
    ASSERT_NE(fcntl(fd, F_SETFL, flags | O_NONBLOCK), -1);
  }

  Endpoint endpoint = IPv4Endpoint{};
  TCPConnection connection(std::move(client_socket), endpoint, false,
                           Role::Client);

  std::atomic<bool> last_write_interest{false};
  connection.set_write_interest_handler(
      [&last_write_interest](bool enabled) {
        last_write_interest.store(enabled);
      });

  TCPTransport receiver_transport =
      PlainTCPTransport(receiver_socket.get_fd());
  ReceiveBuffer receive_buffer;

  const auto payloads = make_payloads();
  ASSERT_EQ(payloads.size(), kPayloadCount);

  std::latch start{1};
  std::atomic<bool> sender_done{false};
  std::atomic<bool> flush_failed{false};
  std::atomic<std::size_t> queued{0};

  std::thread sender([&] {
    start.wait();
    for (const auto &payload : payloads) {
      connection.send(payload);
      queued.fetch_add(1, std::memory_order_release);
    }
    sender_done.store(true, std::memory_order_release);
  });

  std::thread flusher([&] {
    start.wait();
    std::size_t last_flush_queued = 0;
    while (true) {
      const std::size_t queued_now = queued.load(std::memory_order_acquire);
      if (sender_done.load(std::memory_order_acquire)) {
        if (!connection.has_to_write())
          return;
      } else if (queued_now < last_flush_queued + kFlushBatchFrames) {
        std::this_thread::yield();
        continue;
      }

      const auto result = connection.flush();
      if (result.status == IOStatus::Error) {
        flush_failed.store(true);
        return;
      }
      last_flush_queued = queued_now;
    }
  });

  start.count_down();
  sender.join();
  flusher.join();

  EXPECT_FALSE(flush_failed.load());
  EXPECT_FALSE(connection.has_to_write());
  EXPECT_FALSE(last_write_interest.load());

  ASSERT_EQ(receive_buffer.consume(receiver_transport), IOStatus::Complete);

  std::size_t received = 0;
  while (auto message = receive_buffer.next_message()) {
    ASSERT_LT(received, payloads.size());
    EXPECT_EQ(*message, payloads[received]);
    ++received;
  }

  EXPECT_EQ(received, payloads.size());
  EXPECT_EQ(receive_buffer.size(), 0u);
}

} // namespace
