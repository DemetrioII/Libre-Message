#pragma once
#include "timer_id.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

class TimerQueue {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;
  using Callback = std::function<void()>;

  struct ScheduleResult {
    TimerId id{kInvalidTimerId};
    bool became_earlier{false};
  };

  ScheduleResult schedule(TimePoint deadline, Callback callback);
  bool cancel(TimerId id);
  std::optional<TimePoint> next_deadline() const;
  std::size_t run_expired(TimePoint now);

private:
  struct TimerEntry {
    TimerId id;
    Callback callback;
  };

  using Queue = std::multimap<TimePoint, TimerEntry>;

  mutable std::mutex mutex_;
  Queue queue_;
  std::unordered_map<TimerId, Queue::iterator> index_;
  TimerId next_id_{1};
};
