#pragma once
#include "net/network/rawsocket.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <variant>

struct IPv4Endpoint {
  sockaddr_in address{};
};

struct IPv6Endpoint {
  sockaddr_in6 address{};
};

struct UnixEndpoint {
  sockaddr_un address{};
};

using Endpoint = std::variant<IPv4Endpoint, IPv6Endpoint, UnixEndpoint>;

class EndpointManager {
public:
  static int Bind(const Socket &socket, const Endpoint &endpoint);
  static int Connect(const Socket &socket, const Endpoint &endpoint);
};
