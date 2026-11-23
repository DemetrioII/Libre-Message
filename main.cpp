#include "models/message_codec.hpp"
#include "net/network/peer.hpp"

#include "app/manager.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstring>
#include <iomanip>
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
    } else if (command == "contact-export") {
      std::string path;
      if (!(std::cin >> std::quoted(path)))
        break;
      const auto result = manager.export_contact(path);
      if (!result)
        spdlog::error("Contact export failed: {}",
                      contact_error_message(result.error()));
      else
        std::cout << "Contact QR exported to " << path << '\n';
    } else if (command == "contact-import") {
      std::string path;
      if (!(std::cin >> std::quoted(path)))
        break;
      const auto invitation = read_contact_qr(path);
      if (!invitation) {
        spdlog::error("Contact import failed: {}",
                      contact_error_message(invitation.error()));
        continue;
      }
      std::cout << "Contact ID: " << invitation->user_id
                << "\nPublic key SHA-256: "
                << fingerprint_hex(invitation->fingerprint)
                << "\nConfirm this is the person you intend to trust. Add "
                   "contact? [yes/no] "
                << std::flush;
      std::string answer;
      if (!(std::cin >> answer))
        break;
      if (answer != "yes") {
        std::cout << "Contact import cancelled\n";
        continue;
      }
      const auto result = manager.trust_contact(*invitation);
      if (!result)
        spdlog::error("Contact import failed: {}",
                      contact_error_message(result.error()));
      else
        std::cout << "Trusted contact saved: " << invitation->user_id << '\n';
    } else if (command == "contact-show") {
      auto invitation = manager.own_contact();
      if (!invitation.has_value()) {
        spdlog::error("Contact error: {}",
                      contact_error_message(invitation.error()));
      } else {
        std::cout << invitation->user_id << "\nPublic key SHA-256: "
                  << fingerprint_hex(invitation->fingerprint) << std::endl;
      }
    }
  }
  return 0;
}
