#include "net/network/endpoint.hpp"

int EndpointManager::Bind(const Socket &socket, const Endpoint &endpoint) {
  int res = std::visit(
      [&socket](const auto &endpoint) {
        return ::bind(socket.get_fd(), (struct sockaddr *)(&endpoint.address),
                      sizeof(endpoint.address));
      },
      endpoint);
  listen(socket.get_fd(), 10);
  return res;
}

int EndpointManager::Connect(const Socket &socket, const Endpoint &endpoint) {
  return std::visit(
      [&socket](const auto &endpoint) {
        return ::connect(socket.get_fd(),
                         (struct sockaddr *)(&endpoint.address),
                         sizeof(endpoint.address));
      },
      endpoint);
}
