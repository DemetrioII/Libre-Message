#include "net/transport/transport.hpp"
#include <errno.h>
#include <openssl/err.h>
#include <poll.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

static bool wait_for_io(int fd, short events) {
  pollfd pfd{fd, events, 0};
  int rc;
  do {
    rc = ::poll(&pfd, 1, 5000);
  } while (rc == -1 && errno == EINTR);
  return rc > 0;
}

FlushResult PlainTCPTransport::write(std::span<const std::byte> data) {
  const auto n = ::send(fd_, data.data(), data.size(), MSG_NOSIGNAL);

  if (n >= 0) {
    spdlog::info("sent {} bytes to {}", data.size(), fd_);
    return {static_cast<std::size_t>(n), IOStatus::Complete};
  }

  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return {0, IOStatus::WantWrite};

  if (n < 0 && errno == EINTR)
    return {0, IOStatus::WantWrite};

  spdlog::error("error sending {} data bytes to {}, error: {}", data.size(),
                fd_, strerror(errno));
  return {0, IOStatus::Error};
}

FlushResult PlainTCPTransport::read(std::span<std::byte> data) {
  std::size_t total = 0;
  while (total < data.size()) {
    const ssize_t n = ::recv(fd_, data.data() + total, data.size() - total, 0);
    if (n > 0) {
      total += static_cast<std::size_t>(n);
      continue;
    }

    if (n == 0) {
      return {total, IOStatus::Closed};
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return {total, IOStatus::WantRead};
      break;
    }

    if (errno == EINTR) {
      return {total, IOStatus::WantRead};
      continue;
    }

    spdlog::error("error during receiving on TCP connection {}, error: {}",
                  (void *)this, strerror(errno));
    return {total, IOStatus::Error};
  }
  return {total, IOStatus::Complete};
}

PlainTCPTransport::~PlainTCPTransport() {}

TLSState TLSTransport::state() const noexcept { return state_; }

FlushResult TLSTransport::connect() {
  const int ret = SSL_connect(ssl_);

  if (ret == 1) {
    state_ = TLSState::Established;
    return {0, IOStatus::Complete};
  }

  state_ = TLSState::Handshaking;
  switch (SSL_get_error(ssl_, ret)) {
  case SSL_ERROR_WANT_READ:
    return {0, IOStatus::WantRead};

  case SSL_ERROR_WANT_WRITE:
    return {0, IOStatus::WantWrite};

  default:
    state_ = TLSState::Error;
    unsigned long err = ERR_get_error();

    char buffer[256];
    ERR_error_string_n(err, buffer, sizeof(buffer));

    spdlog::error("SSL_connect failed: {}", buffer);
    return {0, IOStatus::Error};
  }
}

FlushResult TLSTransport::accept() {
  const int ret = SSL_accept(ssl_);
  if (ret == 1) {
    state_ = TLSState::Established;
    return {0, IOStatus::Complete};
  }

  state_ = TLSState::Handshaking;
  spdlog::info("accepting using ssl, status: {}", ret);

  switch (SSL_get_error(ssl_, ret)) {
  case SSL_ERROR_WANT_READ:

    return {0, IOStatus::WantRead};

  case SSL_ERROR_WANT_WRITE:

    return {0, IOStatus::WantWrite};

  default:
    state_ = TLSState::Error;
    unsigned long err = ERR_get_error();

    char buffer[256];
    ERR_error_string_n(err, buffer, sizeof(buffer));

    spdlog::error("SSL_accept failed: {}", buffer);
    return {0, IOStatus::Error};
  }
}

TLSTransport::TLSTransport(int fd) : fd_(fd), state_(TLSState::Handshaking) {}

void TLSTransport::set_role(int is_client) {
  if (is_client) {
    ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ctx_)
      throw std::runtime_error("SSL_CTX_new failed");

    SSL_CTX_set_verify(ctx_, SSL_VERIFY_NONE, nullptr);
    ssl_ = SSL_new(ctx_);
    if (!ssl_) {
      SSL_CTX_free(ctx_);
      ctx_ = nullptr;
      throw std::runtime_error("SSL_new failed");
    }

    if (SSL_set_fd(ssl_, fd_) != 1) {
      SSL_free(ssl_);
      ssl_ = nullptr;

      SSL_CTX_free(ctx_);
      ctx_ = nullptr;

      throw std::runtime_error("SSL_set_fd failed");
    }
  } else {
    ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ctx_)
      throw std::runtime_error("SSL_CTX_new failed");

    if (SSL_CTX_use_certificate_file(ctx_, "server.crt", SSL_FILETYPE_PEM) !=
        1) {
      throw std::runtime_error("failed to load certificate");
    }
    if (SSL_CTX_use_PrivateKey_file(ctx_, "server.key", SSL_FILETYPE_PEM) !=
        1) {
      throw std::runtime_error("failed to load private key");
    }

    if (SSL_CTX_check_private_key(ctx_) != 1) {
      throw std::runtime_error("certificate/key mismatch");
    }
    ssl_ = SSL_new(ctx_);
    if (!ssl_) {
      SSL_CTX_free(ctx_);
      ctx_ = nullptr;
      throw std::runtime_error("SSL_new failed");
    }

    if (SSL_set_fd(ssl_, fd_) != 1) {
      SSL_free(ssl_);
      ssl_ = nullptr;

      SSL_CTX_free(ctx_);
      ctx_ = nullptr;

      throw std::runtime_error("SSL_set_fd failed");
    }
  }
}

TLSTransport::~TLSTransport() {
  if (ssl_) {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
  }

  if (ctx_) {
    SSL_CTX_free(ctx_);
  }
}

TLSTransport::TLSTransport(TLSTransport &&other) noexcept
    : ssl_(other.ssl_), ctx_(other.ctx_), fd_(other.fd_), state_(other.state_) {

  other.fd_ = -1;
  other.ctx_ = nullptr;
  other.ssl_ = nullptr;
  other.state_ = TLSState::Closed;
}

TLSTransport &TLSTransport::operator=(TLSTransport &&other) noexcept {
  if (this != &other) {
    if (ssl_) {
      SSL_shutdown(ssl_);
      SSL_free(ssl_);
    }

    if (ctx_) {
      SSL_CTX_free(ctx_);
    }

    fd_ = other.fd_;
    ctx_ = other.ctx_;
    ssl_ = other.ssl_;
    state_ = other.state_;

    other.fd_ = -1;
    other.ctx_ = nullptr;
    other.ssl_ = nullptr;
    other.state_ = TLSState::Closed;
  }

  return *this;
}

FlushResult TLSTransport::write(std::span<const std::byte> data) {
  const auto n = SSL_write(ssl_, data.data(), data.size());

  if (n > 0) {
    spdlog::info("sent {} bytes using TLS transport {}", n, (void *)this);
    return {static_cast<std::size_t>(n), IOStatus::Complete};
  }

  switch (SSL_get_error(ssl_, n)) {
  case SSL_ERROR_WANT_READ:
  case SSL_ERROR_WANT_WRITE:
    return {0, IOStatus::WantWrite};
  case SSL_ERROR_ZERO_RETURN:
    return {0, IOStatus::Closed};
  default: {
    state_ = TLSState::Error;
    unsigned long err = ERR_get_error();
    char buffer[256];
    ERR_error_string_n(err, buffer, sizeof(buffer));
    spdlog::error("SSL_write failed: {}", buffer);
    return {0, IOStatus::Error};
  }
  }
}

FlushResult TLSTransport::read(std::span<std::byte> data) {
  const int n = SSL_read(ssl_, data.data(), data.size());

  if (n > 0) {
    return {static_cast<std::size_t>(n), IOStatus::Complete};
  }

  const int error = SSL_get_error(ssl_, n);

  switch (error) {
  case SSL_ERROR_ZERO_RETURN:
    // TLS connection was closed cleanly.
    return {0, IOStatus::Closed};

  case SSL_ERROR_WANT_READ:
    return {0, IOStatus::WantRead};

  case SSL_ERROR_WANT_WRITE:
    return {0, IOStatus::WantWrite};

  default:
    state_ = TLSState::Error;
    ERR_print_errors_fp(stderr);
    return {0, IOStatus::Error};
  }
}
