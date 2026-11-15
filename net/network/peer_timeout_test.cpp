#include "net/network/peer.hpp"

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <thread>

namespace {

using namespace std::chrono_literals;

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

TEST(TCPPeerTimeoutTest, EstablishmentTimeoutFiresForSilentTlsServer) {
  std::uint16_t port = 0;
  ScopedFd listener(make_silent_listener(port));
  ASSERT_GE(listener.get(), 0);
  ASSERT_NE(port, 0);

  TCPPeer peer("127.0.0.1", 0, 4, true,
               TCPPeerOptions{.establishment_timeout = 150ms});

  std::promise<ConnectionId> disconnected;
  std::atomic<bool> fulfilled{false};
  auto future = disconnected.get_future();
  peer.set_disconnect_handler([&](ConnectionId id) {
    if (!fulfilled.exchange(true))
      disconnected.set_value(id);
  });

  peer.run_event_loop();
  const auto id = peer.connect("127.0.0.1", port, 4);
  ASSERT_NE(id, kInvalidConnectionId);

  ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(future.get(), id);
}

TEST(TCPPeerTimeoutTest, EstablishedConnectionCancelsTimer) {
  const auto port = reserve_port();
  ASSERT_NE(port, 0);

  TCPPeer server("127.0.0.1", port, 4, false,
                 TCPPeerOptions{.establishment_timeout = 200ms});
  TCPPeer client("127.0.0.1", 0, 4, false,
                 TCPPeerOptions{.establishment_timeout = 200ms});

  std::atomic<int> disconnects{0};
  server.set_disconnect_handler([&](ConnectionId) { ++disconnects; });
  client.set_disconnect_handler([&](ConnectionId) { ++disconnects; });

  server.run_event_loop();
  client.run_event_loop();

  const auto id = client.connect("127.0.0.1", port, 4);
  ASSERT_NE(id, kInvalidConnectionId);

  std::this_thread::sleep_for(500ms);
  EXPECT_EQ(disconnects.load(), 0);
}

TEST(TCPPeerTimeoutTest, IdleTimeoutClosesEstablishedConnection) {
  const auto port = reserve_port();
  ASSERT_NE(port, 0);

  TCPPeer server("127.0.0.1", port, 4, false);
  TCPPeer client("127.0.0.1", 0, 4, false,
                 TCPPeerOptions{.establishment_timeout = 1s,
                                .idle_timeout = 150ms});

  std::promise<ConnectionId> disconnected;
  std::atomic<bool> fulfilled{false};
  auto future = disconnected.get_future();
  client.set_disconnect_handler([&](ConnectionId id) {
    if (!fulfilled.exchange(true))
      disconnected.set_value(id);
  });

  server.run_event_loop();
  client.run_event_loop();

  const auto id = client.connect("127.0.0.1", port, 4);
  ASSERT_NE(id, kInvalidConnectionId);

  ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
  EXPECT_EQ(future.get(), id);
}

TEST(TCPPeerTimeoutTest, ReadActivityResetsIdleTimer) {
  const auto port = reserve_port();
  ASSERT_NE(port, 0);

  TCPPeer server("127.0.0.1", port, 4, false);
  TCPPeer client("127.0.0.1", 0, 4, false,
                 TCPPeerOptions{.establishment_timeout = 1s,
                                .idle_timeout = 300ms});

  client.set_sender_id("client");

  std::promise<ConnectionId> server_connection;
  std::atomic<bool> server_fulfilled{false};
  auto server_future = server_connection.get_future();
  server.set_message_handler([&](ConnectionId id, MessageModel) {
    if (!server_fulfilled.exchange(true))
      server_connection.set_value(id);
  });

  std::atomic<int> client_disconnects{0};
  client.set_disconnect_handler(
      [&](ConnectionId) { ++client_disconnects; });

  server.run_event_loop();
  client.run_event_loop();

  const auto client_id = client.connect("127.0.0.1", port, 4);
  ASSERT_NE(client_id, kInvalidConnectionId);
  ASSERT_EQ(server_future.wait_for(2s), std::future_status::ready);
  const auto server_id = server_future.get();

  std::array<std::byte, 1> payload{std::byte{0x00}};
  for (int i = 0; i < 8; ++i) {
    std::this_thread::sleep_for(50ms);
    server.send_data(server_id, payload);
  }

  EXPECT_EQ(client_disconnects.load(), 0);
}

TEST(TCPPeerTimeoutTest, WriteActivityResetsIdleTimer) {
  const auto port = reserve_port();
  ASSERT_NE(port, 0);

  TCPPeer server("127.0.0.1", port, 4, false);
  TCPPeer client("127.0.0.1", 0, 4, false,
                 TCPPeerOptions{.establishment_timeout = 1s,
                                .idle_timeout = 300ms});

  client.set_sender_id("client");

  std::promise<ConnectionId> server_connection;
  std::atomic<bool> server_fulfilled{false};
  auto server_future = server_connection.get_future();
  server.set_message_handler([&](ConnectionId id, MessageModel) {
    if (!server_fulfilled.exchange(true))
      server_connection.set_value(id);
  });

  std::atomic<int> client_disconnects{0};
  client.set_disconnect_handler(
      [&](ConnectionId) { ++client_disconnects; });

  server.run_event_loop();
  client.run_event_loop();

  const auto id = client.connect("127.0.0.1", port, 4);
  ASSERT_NE(id, kInvalidConnectionId);
  ASSERT_EQ(server_future.wait_for(2s), std::future_status::ready);

  std::array<std::byte, 1> payload{std::byte{0x00}};
  for (int i = 0; i < 8; ++i) {
    std::this_thread::sleep_for(50ms);
    client.send_data(id, payload);
  }

  EXPECT_EQ(client_disconnects.load(), 0);
}

} // namespace
