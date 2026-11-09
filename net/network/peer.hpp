#pragma once
#include "models/message.hpp"
#include "net/event_loop/event_loop.hpp"
#include "net/network/handlers.hpp"
#include "net/network/listener.hpp"
#include <algorithm>
#include <thread>
#include <unordered_map>
#include <utility>

class TCPPeer {
  bool use_tls_{false};
  std::mutex connection_map_mutex_;
  std::unordered_map<int, std::shared_ptr<TCPConnection>> connections_;
  std::unordered_map<int, std::shared_ptr<TCPConnectionEventHandler>>
      connection_handlers_;
  std::unique_ptr<EventLoop> event_loop_;
  Endpoint endpoint_;

  std::shared_ptr<TCPListener> listener_;
  std::shared_ptr<TCPListenerEventHandler> tcp_listener_handler_;

  std::jthread event_loop_jthread_;

  std::string sender_id_;

  std::function<void(int, MessageModel)> on_message_;

  void on_read(int fd);

  void on_write(int fd);

  void make_tcp_connection(TCPConnection &&connection);

  void on_disconnect(int fd);

  std::vector<std::byte> make_handshake_message() const;

public:
  TCPPeer(std::string_view host, short port, int ip_version, bool use_tls);

  int connect(std::string_view host, short port, int ip_version);

  void disconnect(std::string_view host, short port);

  void run_event_loop();

  void send_data(int fd, std::span<std::byte> data);

  void broadcast(std::span<std::byte> data);

  void set_sender_id(std::string id);

  void set_message_handler(std::function<void(int, MessageModel)> handler);
};
