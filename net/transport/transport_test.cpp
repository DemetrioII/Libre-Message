#include "models/framer.hpp"
#include "net/transport/receivebuffer.hpp"
#include "net/transport/sendbuffer.hpp"

#include <fcntl.h>
#include <chrono>
#include <memory>
#include <utility>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>

class TransportTest : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
    for (auto fd : fds_) {
      fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    }
  }

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

  const std::vector<std::byte> payload = {std::byte('h'), std::byte('e'),
                                          std::byte('l'), std::byte('l'),
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

namespace {

TEST_F(TransportTest, PeerFingerprintBeforeTlsSetupReturnsNotEstablished) {
  TLSTransport transport(fds_[0]);
  const auto result = transport.peer_fingerprint();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), PeerFingerprintError::NoEstablished);
}

TEST_F(TransportTest, PeerFingerprintBeforeHandshakeReturnsNotEstablished) {
  TLSTransport transport(fds_[0]);
  transport.set_role(1);
  const auto result = transport.peer_fingerprint();
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), PeerFingerprintError::NoEstablished);
}

TEST_F(TransportTest, PeerFingerprintUsesEstablishedPeersCertificate) {
  // An in-memory TLS server avoids certificate files and changes to the cwd.
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
  ASSERT_TRUE(key);
  std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
  ASSERT_TRUE(cert);
  ASSERT_EQ(X509_set_version(cert.get(), 2), 1);
  ASSERT_EQ(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1), 1);
  ASSERT_NE(X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0), nullptr);
  ASSERT_NE(X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600), nullptr);
  ASSERT_EQ(X509_set_pubkey(cert.get(), key.get()), 1);
  auto name = X509_get_subject_name(cert.get());
  ASSERT_EQ(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
      reinterpret_cast<const unsigned char *>("peer-fingerprint-test"),
      -1, -1, 0), 1);
  ASSERT_EQ(X509_set_issuer_name(cert.get(), name), 1);
  ASSERT_GT(X509_sign(cert.get(), key.get(), EVP_sha256()), 0);

  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(
      SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
  ASSERT_TRUE(context);
  ASSERT_EQ(SSL_CTX_use_certificate(context.get(), cert.get()), 1);
  ASSERT_EQ(SSL_CTX_use_PrivateKey(context.get(), key.get()), 1);
  std::unique_ptr<SSL, decltype(&SSL_free)> server(SSL_new(context.get()), SSL_free);
  ASSERT_TRUE(server);
  ASSERT_EQ(SSL_set_fd(server.get(), fds_[1]), 1);

  TLSTransport client(fds_[0]);
  client.set_role(1);
  bool server_ready = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while ((!server_ready || client.state() != TLSState::Established) &&
         std::chrono::steady_clock::now() < deadline) {
    if (client.state() != TLSState::Established) {
      const auto result = client.connect();
      ASSERT_TRUE(result.status == IOStatus::Complete ||
                  result.status == IOStatus::WantRead ||
                  result.status == IOStatus::WantWrite);
    }
    if (!server_ready) {
      const int result = SSL_accept(server.get());
      if (result == 1) {
        server_ready = true;
      } else {
        const int error = SSL_get_error(server.get(), result);
        ASSERT_TRUE(error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE);
      }
    }
  }
  ASSERT_TRUE(server_ready);
  ASSERT_EQ(client.state(), TLSState::Established);
  const auto expected = certificate_fingerprint(cert.get());
  ASSERT_TRUE(expected);
  const auto actual = client.peer_fingerprint();
  ASSERT_TRUE(actual);
  EXPECT_EQ(*actual, *expected);
  // Querying twice must neither consume nor prematurely free the certificate.
  EXPECT_EQ(client.peer_fingerprint(), actual);

  TLSTransport moved(std::move(client));
  EXPECT_EQ(moved.peer_fingerprint(), actual);
  const auto moved_from = client.peer_fingerprint();
  ASSERT_FALSE(moved_from);
  EXPECT_EQ(moved_from.error(), PeerFingerprintError::NoEstablished);
}

} // namespace

TEST_F(TransportTest, PartialFrameAcrossReads) {
  TCPTransport sender = PlainTCPTransport(fds_[0]);
  TCPTransport receiver = PlainTCPTransport(fds_[1]);

  const std::vector<std::byte> payload = {std::byte('p'), std::byte('a'),
                                          std::byte('r'), std::byte('t'),
                                          std::byte('i'), std::byte('a'),
                                          std::byte('l')};
  const auto framed = Framer::frame(payload);
  ASSERT_GT(framed.size(), 3u);

  SendBuffer<> send_buf;
  ReceiveBuffer recv_buf;

  send_buf.send(std::span(framed.data(), 3));
  ASSERT_EQ(send_buf.flush(sender).status, IOStatus::Complete);

  ASSERT_EQ(recv_buf.consume(receiver), IOStatus::Complete);
  EXPECT_EQ(recv_buf.size(), 3u);
  EXPECT_FALSE(recv_buf.next_message().has_value());

  send_buf.send(std::span(framed.data() + 3, framed.size() - 3));
  ASSERT_EQ(send_buf.flush(sender).status, IOStatus::Complete);

  ASSERT_EQ(recv_buf.consume(receiver), IOStatus::Complete);

  const auto message = recv_buf.next_message();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(*message, payload);
  EXPECT_EQ(recv_buf.size(), 0u);
}

TEST_F(TransportTest, MultipleFramesInSingleConsume) {
  TCPTransport sender = PlainTCPTransport(fds_[0]);
  TCPTransport receiver = PlainTCPTransport(fds_[1]);

  const std::vector<std::byte> first = {std::byte('o'), std::byte('n'),
                                        std::byte('e')};
  const std::vector<std::byte> second = {std::byte('t'), std::byte('w'),
                                         std::byte('o')};

  SendBuffer<> send_buf;
  send_buf.send(Framer::frame(first));
  send_buf.send(Framer::frame(second));
  ASSERT_EQ(send_buf.flush(sender).status, IOStatus::Complete);

  ReceiveBuffer recv_buf;
  ASSERT_EQ(recv_buf.consume(receiver), IOStatus::Complete);

  const auto first_msg = recv_buf.next_message();
  ASSERT_TRUE(first_msg.has_value());
  EXPECT_EQ(*first_msg, first);

  const auto second_msg = recv_buf.next_message();
  ASSERT_TRUE(second_msg.has_value());
  EXPECT_EQ(*second_msg, second);

  EXPECT_FALSE(recv_buf.next_message().has_value());
  EXPECT_EQ(recv_buf.size(), 0u);
}

TEST_F(TransportTest, PeerShutdownReportsClosed) {
  TCPTransport receiver = PlainTCPTransport(fds_[1]);
  ASSERT_EQ(::shutdown(fds_[0], SHUT_WR), 0);

  ReceiveBuffer recv_buf;
  EXPECT_EQ(recv_buf.consume(receiver), IOStatus::Closed);
  EXPECT_EQ(recv_buf.size(), 0u);
}

TEST_F(TransportTest, DataBeforeShutdownIsDelivered) {
  TCPTransport sender = PlainTCPTransport(fds_[0]);
  TCPTransport receiver = PlainTCPTransport(fds_[1]);

  const std::vector<std::byte> payload = {std::byte('b'), std::byte('y'),
                                          std::byte('e')};
  SendBuffer<> send_buf;
  send_buf.send(Framer::frame(payload));
  ASSERT_EQ(send_buf.flush(sender).status, IOStatus::Complete);
  ASSERT_EQ(::shutdown(fds_[0], SHUT_WR), 0);

  ReceiveBuffer recv_buf;
  ASSERT_EQ(recv_buf.consume(receiver), IOStatus::Closed);

  const auto message = recv_buf.next_message();
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(*message, payload);
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
