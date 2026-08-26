#pragma once
#include <stdint.h>

struct Event {
  int fd;
  uint32_t events;
};

class EventHandler {
public:
  virtual ~EventHandler() = default;

  virtual void handle(const Event &event) = 0;
};
