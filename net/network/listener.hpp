#pragma once
#include "net/network/connection.hpp"
#include "net/network/endpoint.hpp"
#include "net/network/rawsocket.hpp"

enum class TCPListenerError {
  WrongProtocol,
  BindError,
  AcceptError,
  ListenError,
};

using UDPListenerError = TCPListenerError;

class TCPListener {
  Socket socket_;
  Endpoint endpoint_;

public:
  TCPListener(Socket &&sock);

  static std::expected<TCPListener, TCPListenerError>
  bind(const Endpoint &endpoint);

  std::expected<TCPConnection, TCPListenerError> accept();

  const Socket &socket() const;
};

class UDPListener {
  Socket socket_;
  Endpoint endpoint_;

public:
  UDPListener(Socket &&socket);

  static std::expected<UDPListener, UDPListenerError>
  bind(const Endpoint &endpoint);

  const Socket &socket() const;
};
