#pragma once
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

class Socket {
  int fd{-1};

public:
  Socket(int domain, int type, int protocol);
  Socket(int FD);
  ~Socket();

  int get_fd() const;

  Socket(const Socket &) = delete;
  Socket &operator=(const Socket &) = delete;

  Socket(Socket &&) noexcept;
  Socket &operator=(Socket &&) noexcept;
};
