#pragma once
#include "net/network/connection.hpp"
#include "net/network/endpoint.hpp"
#include "net/network/rawsocket.hpp"

enum class TCPListenerError {
  WrongProtocol,
  BindError,
  AcceptError,
};

class TCPListener {
  Socket socket_;

public:
  TCPListener(Socket &&sock);

  static std::expected<TCPListener, TCPListenerError>
  bind(const Endpoint &endpoint);

  std::expected<TCPConnection, TCPListenerError> accept();
};
