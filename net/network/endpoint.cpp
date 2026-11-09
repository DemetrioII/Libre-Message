#include "net/network/endpoint.hpp"
#include <arpa/inet.h>
#include <spdlog/spdlog.h>
#include <stdexcept>

int EndpointManager::Bind(const Socket &socket, const Endpoint &endpoint) {
  int res = std::visit(
      [&socket](const auto &endpoint) {
        return ::bind(socket.get_fd(), (struct sockaddr *)(&endpoint.address),
                      sizeof(endpoint.address));
      },
      endpoint);
  if (res != 0) {
    spdlog::error("error binding socket {} to endpoint {}", (void *)&socket,
                  (void *)&endpoint);
  }
  return res;
}

int EndpointManager::Connect(const Socket &socket, const Endpoint &endpoint) {
  int res = std::visit(
      [&socket](const auto &endpoint) {
        return ::connect(socket.get_fd(),
                         (struct sockaddr *)(&endpoint.address),
                         sizeof(endpoint.address));
      },
      endpoint);
  if (res != 0) {
    spdlog::error("error connecting from socket {} to endpoint {}",
                  (void *)&socket, (void *)&endpoint);
  }
  return res;
}

Endpoint EndpointManager::Construct(int ip_version, std::string_view host,
                                    uint16_t port) {
  switch (ip_version) {
  case 4: {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.data(), &addr.sin_addr);
    IPv4Endpoint endpoint{.address = addr};
    spdlog::info("created new endpoint {} IPv4:\n"
                 "address:	{}\n"
                 "port:		{}\n",
                 (void *)&endpoint, host, port);
    return endpoint;
  }
  case 6: {
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(port);
    inet_pton(AF_INET6, host.data(), &addr.sin6_addr);
    IPv6Endpoint endpoint{.address = addr};
    spdlog::info("created new endpoint {} IPv6:\n"
                 "address:	{}\n"
                 "port:		{}\n",
                 (void *)&endpoint, host, port);
    return endpoint;
  }
  default:
    throw std::runtime_error("unsupported IP version " +
                             std::to_string(ip_version));
  }
}

int EndpointManager::FamilyToVersion(int family) {
  switch (family) {
  case AF_INET:
    return 4;
  case AF_INET6:
    return 6;
  default:
    return -1;
  }
}
