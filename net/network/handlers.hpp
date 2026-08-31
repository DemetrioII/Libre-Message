#pragma once
#include "net/event_loop/event_handler.hpp"
#include "net/network/listener.hpp"
#include <functional>
#include <memory>
#include <mutex>

using ModifyEventsHandler = std::function<void(int, uint32_t)>;
using ReadHandler = std::function<void(int)>;
using WriteHandler = std::function<void(int)>;
using DisconnectHandler = std::function<void(int)>;

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

  ReadHandler on_readable_;

  WriteHandler on_writable_;

  DisconnectHandler on_disconnected_;

  ModifyEventsHandler modify_events_;

public:
  TCPConnectionEventHandler(int, std::shared_ptr<TCPListener>,
                            std::shared_ptr<TCPConnection> connection,
                            const ModifyEventsHandler &, const ReadHandler &,
                            const WriteHandler &, const DisconnectHandler &);

  void handle(const Event &event) override;
  ~TCPConnectionEventHandler() override;
};
