#include "net/network/rawsocket.hpp"
#include <iostream>

Socket::Socket(int domain, int type, int protocol)
    : fd(socket(domain, type, protocol)) {}

Socket::Socket(Socket &&other) noexcept {
  fd = other.fd;
  other.fd = -1;
}

Socket::~Socket() {
  if (fd != -1) {
    ::close(fd);
  }
  std::cout << "SOCKET FD=" << fd << " CLOSED!" << std::endl;
  fd = -1;
}

Socket &Socket::operator=(Socket &&socket) noexcept {
  if (this != &socket) {
    if (fd != -1)
      ::close(fd);
    fd = socket.fd;
    socket.fd = -1;
  }
  return *this;
}

int Socket::get_fd() const { return fd; }

Socket::Socket(int FD) : fd(FD) {}
