#include "net/network/handlers.hpp"

#include <gtest/gtest.h>
#include <string>
#include <sys/epoll.h>
#include <vector>

namespace {

struct Calls {
  std::vector<std::string> names;
};

TCPConnectionEventHandler
make_handler(int fd, Calls &calls) {
  return TCPConnectionEventHandler(
      fd, nullptr, nullptr, [](int, uint32_t) {},
      [&calls](int) { calls.names.push_back("read"); },
      [&calls](int) { calls.names.push_back("write"); },
      [&calls](int) { calls.names.push_back("disconnect"); });
}

} // namespace

TEST(HandlersTest, ReadBeforeRdhupDisconnect) {
  Calls calls;
  auto handler = make_handler(7, calls);

  handler.handle({.fd = 7, .events = EPOLLIN | EPOLLRDHUP});

  ASSERT_EQ(calls.names.size(), 2u);
  EXPECT_EQ(calls.names[0], "read");
  EXPECT_EQ(calls.names[1], "disconnect");
}

TEST(HandlersTest, RdhupWithoutReadableDisconnects) {
  Calls calls;
  auto handler = make_handler(7, calls);

  handler.handle({.fd = 7, .events = EPOLLRDHUP});

  ASSERT_EQ(calls.names.size(), 1u);
  EXPECT_EQ(calls.names[0], "disconnect");
}

TEST(HandlersTest, ErrorDisconnectsWithoutReading) {
  Calls calls;
  auto handler = make_handler(7, calls);

  handler.handle({.fd = 7, .events = EPOLLIN | EPOLLERR});

  ASSERT_EQ(calls.names.size(), 1u);
  EXPECT_EQ(calls.names[0], "disconnect");
}

TEST(HandlersTest, ReadAndWriteWithoutHangup) {
  Calls calls;
  auto handler = make_handler(7, calls);

  handler.handle({.fd = 7, .events = EPOLLIN | EPOLLOUT});

  ASSERT_EQ(calls.names.size(), 2u);
  EXPECT_EQ(calls.names[0], "read");
  EXPECT_EQ(calls.names[1], "write");
}

TEST(HandlersTest, ReadBeforeHupDisconnect) {
  Calls calls;
  auto handler = make_handler(7, calls);

  handler.handle({.fd = 7, .events = EPOLLIN | EPOLLHUP});

  ASSERT_EQ(calls.names.size(), 2u);
  EXPECT_EQ(calls.names[0], "read");
  EXPECT_EQ(calls.names[1], "disconnect");
}
