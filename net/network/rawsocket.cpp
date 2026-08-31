#include "net/network/rawsocket.hpp"
#include <iostream>
#include <spdlog/spdlog.h>

Socket::Socket(int domain, int type, int protocol)
    : fd(socket(domain, type, protocol)) {
  spdlog::info("socket {} with fd={} was created", (void *)this, fd);
}

Socket::Socket(Socket &&other) noexcept {
  spdlog::debug("moving socket from {} (fd={}) to {} (old fd={})",
                (void *)&other, other.fd, (void *)this, fd);
  fd = other.fd;
  other.fd = -1;
}

Socket::~Socket() {
  if (fd != -1) {
    ::close(fd);
  }
  spdlog::info("socket {} with fd={} closed", (void *)this, fd);
  fd = -1;
}

Socket &Socket::operator=(Socket &&socket) noexcept {
  if (this != &socket) {
    if (fd != -1)
      ::close(fd);
    spdlog::debug("moving socket from {} (fd={}) to {} (old fd={})",
                  (void *)&socket, socket.fd, (void *)this, fd);
    fd = socket.fd;
    socket.fd = -1;
  }
  return *this;
}

int Socket::get_fd() const { return fd; }

Socket::Socket(int FD) : fd(FD) {}
