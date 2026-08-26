#pragma once
#include "net/event_loop/event_handler.hpp"
#include "net/network/listener.hpp"
#include <functional>
#include <memory>
#include <mutex>

class TCPListenerEventHandler : public EventHandler {
  std::weak_ptr<TCPListener> listener_;

  std::function<void(TCPConnection &&)>
      connection_callback_; // здесь предполагается добавление в мапу или что-то
                            // похожее
public:
  TCPListenerEventHandler(std::shared_ptr<TCPListener>,
                          const std::function<void(TCPConnection &&)>);
  void handle(const Event &event) override;
  ~TCPListenerEventHandler() override;
};

class TCPConnectionEventHandler : public EventHandler {
  std::weak_ptr<TCPListener> listener_;
  int fd_;

  std::weak_ptr<TCPConnection> connection_;

  void handle_disconnect();

  std::function<void(int fd)> on_readable_;

  std::function<void(int fd)> on_writable_;

  std::function<void(int fd)> on_disconnected_;

  std::function<void(int fd, uint32_t)> enable_writing_;

public:
  TCPConnectionEventHandler(int, std::shared_ptr<TCPListener>,
                            std::shared_ptr<TCPConnection> connection,
                            const std::function<void(int, uint32_t)> &,
                            const std::function<void(int)> &,
                            const std::function<void(int)> &,
                            const std::function<void(int)> &);

  void handle(const Event &event) override;
  ~TCPConnectionEventHandler() override;
};
