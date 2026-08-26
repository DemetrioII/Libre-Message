#include "net/network/listener.hpp"
#include <iostream>
#include <stdexcept>

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

TCPListener::TCPListener(Socket &&socket) : socket_(std::move(socket)) {}

std::expected<TCPListener, TCPListenerError>
TCPListener::bind(const Endpoint &endpoint) {
  auto listener = std::visit(
      overloaded{[](const IPv4Endpoint &endpoint)
                     -> std::expected<TCPListener, TCPListenerError> {
                   auto socket = Socket(AF_INET, SOCK_STREAM, 0);
                   TCPListener listener{std::move(socket)};
                   return listener;
                 },
                 [](const IPv6Endpoint &endpoint)
                     -> std::expected<TCPListener, TCPListenerError> {
                   auto socket = Socket(AF_INET6, SOCK_STREAM, 0);
                   TCPListener listener{std::move(socket)};
                   return listener;
                 },
                 [](const auto &enpoint)
                     -> std::expected<TCPListener, TCPListenerError> {
                   return std::unexpected(TCPListenerError::WrongProtocol);
                 }},
      endpoint);
  fcntl(listener->socket_.get_fd(), F_SETFL,
        fcntl(listener->socket_.get_fd(), F_GETFL, 0) | O_NONBLOCK);
  if (!listener.has_value())
    return listener;
  if (EndpointManager::Bind(listener->socket_, endpoint) == -1)
    return std::unexpected(TCPListenerError::BindError);
  listener->endpoint_ = endpoint;
  return listener;
}

std::expected<TCPConnection, TCPListenerError> TCPListener::accept() {
  sockaddr_storage addr_storage{};
  socklen_t addr_len = sizeof(addr_storage);
  int fd = ::accept(socket_.get_fd(),
                    reinterpret_cast<sockaddr *>(&addr_storage), &addr_len);
  if (fd == -1)
    return std::unexpected(TCPListenerError::AcceptError);

  std::string host;
  uint16_t port = 0;

  if (addr_storage.ss_family == AF_INET) {
    auto *addr = reinterpret_cast<const sockaddr_in *>(&addr_storage);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip_str, INET_ADDRSTRLEN);
    host = ip_str;
    port = ntohs(addr->sin_port);
  } else if (addr_storage.ss_family == AF_INET6) {
    auto *addr = reinterpret_cast<const sockaddr_in6 *>(&addr_storage);
    char ip_str[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &addr->sin6_addr, ip_str, INET6_ADDRSTRLEN);
    host = ip_str;
    port = ntohs(addr->sin6_port);
  }

  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  return TCPConnection{
      Socket{fd}, EndpointManager::Construct(
                      EndpointManager::FamilyToVersion(addr_storage.ss_family),
                      host, port)};
}

const Socket &TCPListener::socket() const { return socket_; }

UDPListener::UDPListener(Socket &&socket) : socket_(std::move(socket)) {}

std::expected<UDPListener, UDPListenerError>
UDPListener::bind(const Endpoint &endpoint) {
  auto listener = std::visit(
      overloaded{[](const IPv4Endpoint &endpoint)
                     -> std::expected<UDPListener, UDPListenerError> {
                   auto socket = Socket(AF_INET, SOCK_DGRAM, 0);
                   UDPListener listener{std::move(socket)};
                   return listener;
                 },
                 [](const IPv6Endpoint &endpoint)
                     -> std::expected<UDPListener, UDPListenerError> {
                   auto socket = Socket(AF_INET6, SOCK_DGRAM, 0);
                   UDPListener listener{std::move(socket)};
                   return listener;
                 },
                 [](const auto &enpoint)
                     -> std::expected<UDPListener, UDPListenerError> {
                   return std::unexpected(UDPListenerError::WrongProtocol);
                 }},
      endpoint);
  fcntl(listener->socket_.get_fd(), F_SETFL,
        fcntl(listener->socket_.get_fd(), F_GETFL, 0) | O_NONBLOCK);
  if (!listener.has_value())
    return listener;
  if (EndpointManager::Bind(listener->socket_, endpoint) == -1)
    return std::unexpected(UDPListenerError::BindError);
  listener->endpoint_ = endpoint;
  return listener;
}

const Socket &UDPListener::socket() const { return socket_; }
