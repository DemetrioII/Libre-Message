#include "models/message_codec.hpp"
#include "net/network/peer.hpp"

#include "app/manager.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iostream>
#include <list>
#include <spdlog/spdlog.h>
#include <vector>

int main() {
  uint16_t listen_port;
  spdlog::set_level(spdlog::level::trace);
  std::cin >> listen_port;
  Manager manager{"127.0.0.1", listen_port, 4, true};
  manager.run_event_loop();
  std::cout << "Listening on 127.0.0.1:" << listen_port << "...\n";

  std::string command;
  while (std::cin >> command) {
    if (command == "send") {
      std::string recipient_id;
      std::string msg_to_send;
      std::cin >> recipient_id >> msg_to_send;

      manager.send(recipient_id, std::move(msg_to_send));
    } else if (command == "connect") {
      uint16_t port;
      std::cin >> port;
      auto res = manager.connect("127.0.0.1", port, 4);
      if (!res)
        spdlog::error("connection failed");
    }
  }
  return 0;
}
