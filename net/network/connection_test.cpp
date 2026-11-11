#include "net/network/connection.hpp"
#include "net/network/endpoint.hpp"
#include "net/transport/transport.hpp"

#include <cstddef>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

class ConnectionTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    for (int fd : fds_) {
      const int flags = fcntl(fd, F_GETFL, 0);
      ASSERT_NE(flags, -1);
      ASSERT_NE(fcntl(fd, F_SETFL, flags | O_NONBLOCK), -1);
    }

    endpoint_ = EndpointManager::Construct(4, "127.0.0.1", 0);
  }

  void TearDown() override {
    if (fds_[0] != -1)
      ::close(fds_[0]);
    if (fds_[1] != -1)
      ::close(fds_[1]);
  }

  TCPConnection make_connection(Role role, bool use_tls = false) {
    Socket socket(fds_[0]);
    fds_[0] = -1;
    return TCPConnection(std::move(socket), endpoint_, use_tls, role);
  }

  int fds_[2] = {-1, -1};
  Endpoint endpoint_;
};

TEST_F(ConnectionTest, MovePreservesEstablishedState) {
  auto connection = make_connection(Role::Client);
  ASSERT_EQ(connection.continue_handshake().status, IOStatus::Complete);
  ASSERT_EQ(connection.state(), ConnectionState::Established);

  TCPConnection moved(std::move(connection));
  EXPECT_EQ(moved.state(), ConnectionState::Established);

  TCPConnection assigned;
  assigned = std::move(moved);
  EXPECT_EQ(assigned.state(), ConnectionState::Established);
}

TEST_F(ConnectionTest, TlsClientRoleStartsConnectHandshake) {
  auto connection = make_connection(Role::Client, true);

  const auto result = connection.continue_handshake();

  EXPECT_EQ(result.status, IOStatus::WantRead);
  EXPECT_EQ(connection.state(), ConnectionState::TlsHandshaking);
}

TEST_F(ConnectionTest, MovePreservesBufferedWriteState) {
  auto connection = make_connection(Role::Client);
  bool write_interest = false;
  connection.set_write_interest_handler(
      [&](bool enabled) { write_interest = enabled; });

  const std::vector<std::byte> payload = {std::byte('h'), std::byte('i')};
  connection.send(payload);
  ASSERT_TRUE(connection.has_to_write());
  ASSERT_TRUE(write_interest);

  TCPConnection moved(std::move(connection));
  EXPECT_TRUE(moved.has_to_write());

  const auto result = moved.flush();
  EXPECT_EQ(result.status, IOStatus::Complete);
  EXPECT_GT(result.bytes_sent, 0u);
  EXPECT_FALSE(moved.has_to_write());
  EXPECT_FALSE(write_interest);
}

} // namespace
