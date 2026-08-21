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
  if (!listener.has_value())
    return listener;
  if (EndpointManager::Bind(listener->socket_, endpoint) == -1)
    return std::unexpected(TCPListenerError::BindError);
  std::cout << "Успешно запущен сервер по адресу 127.0.0.1:8080!" << std::endl;
  return listener;
}

std::expected<TCPConnection, TCPListenerError> TCPListener::accept() {
  int fd = ::accept(socket_.get_fd(), nullptr, nullptr);
  if (fd == -1)
    return std::unexpected(TCPListenerError::AcceptError);
  return TCPConnection{Socket{fd}};
}
