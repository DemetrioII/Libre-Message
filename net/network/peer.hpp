#pragma once
#include "models/message.hpp"
#include "net/event_loop/event_loop.hpp"
#include "net/network/handlers.hpp"
#include "net/network/listener.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <utility>

struct TCPPeerOptions {
  std::chrono::milliseconds establishment_timeout{10'000};
  std::chrono::milliseconds idle_timeout{0};
};

class TCPPeer {
  bool use_tls_{false};
  TCPPeerOptions options_;
  std::mutex connection_map_mutex_;
  std::unordered_map<int, ConnectionEntry> connections_;
  std::unordered_map<ConnectionId, int> id_to_fd_;
  ConnectionId next_connection_id_{1};
  std::unordered_map<int, std::shared_ptr<TCPConnectionEventHandler>>
      connection_handlers_;
  std::unique_ptr<EventLoop> event_loop_;
  Endpoint endpoint_;

  std::shared_ptr<TCPListener> listener_;
  std::shared_ptr<TCPListenerEventHandler> tcp_listener_handler_;

  std::jthread event_loop_jthread_;

  std::string sender_id_;

  std::function<void(ConnectionId, MessageModel)> on_message_;

  std::function<void(ConnectionId)> on_disconnect_;

  void on_read(ConnectionId id);

  void on_write(ConnectionId id);

  ConnectionId make_tcp_connection(TCPConnection &&connection);

  void on_disconnect(ConnectionId id);

  void on_establishment_timeout(ConnectionId id);

  void on_established(ConnectionId id);

  void on_idle_timeout(ConnectionId id);

  void mark_activity(ConnectionId id);

  void maybe_cancel_establishment_timer(ConnectionId id);

  void handle_handshake_result(ConnectionId id, const FlushResult &result);

  std::vector<std::byte> make_handshake_message() const;

public:
  TCPPeer(std::string_view host, short port, int ip_version, bool use_tls,
          TCPPeerOptions options = {});

  ~TCPPeer();

  ConnectionId connect(std::string_view host, short port, int ip_version);

  void disconnect(std::string_view host, short port);

  void run_event_loop();

  void send_data(ConnectionId id, std::span<std::byte> data);

  void broadcast(std::span<std::byte> data);

  void set_sender_id(const std::string &id);

  void
  set_message_handler(std::function<void(ConnectionId, MessageModel)> handler);

  void set_disconnect_handler(std::function<void(ConnectionId)> handler);
};
