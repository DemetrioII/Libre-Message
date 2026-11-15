#include "timer_queue.hpp"

TimerQueue::ScheduleResult TimerQueue::schedule(TimerQueue::TimePoint deadline,
                                                Callback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  const TimerId id = next_id_++;
  auto it = queue_.emplace(deadline, TimerEntry{id, std::move(callback)});
  index_.emplace(id, it);
  return {id, it == queue_.begin()};
}

bool TimerQueue::cancel(TimerId id) {
  if (id == kInvalidTimerId)
    return false;

  std::lock_guard<std::mutex> lock(mutex_);
  auto idx = index_.find(id);
  if (idx == index_.end())
    return false;

  queue_.erase(idx->second);
  index_.erase(idx);
  return true;
}

std::optional<TimerQueue::TimePoint> TimerQueue::next_deadline() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty())
    return std::nullopt;
  return queue_.begin()->first;
}

std::size_t TimerQueue::run_expired(TimePoint now) {
  std::vector<Callback> expired;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty() && queue_.begin()->first <= now) {
      auto it = queue_.begin();
      expired.push_back(std::move(it->second.callback));
      index_.erase(it->second.id);
      queue_.erase(it);
    }
  }

  for (auto &callback : expired)
    callback();

  return expired.size();
}
