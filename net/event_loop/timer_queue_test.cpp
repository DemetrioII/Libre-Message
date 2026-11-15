#include "net/event_loop/timer_queue.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <vector>

namespace {

using namespace std::chrono_literals;

TEST(TimerQueueTest, RunsExpiredTimersInDeadlineOrder) {
  TimerQueue queue;
  std::vector<int> order;
  const auto now = TimerQueue::Clock::now();

  queue.schedule(now + 20ms, [&order] { order.push_back(2); });
  queue.schedule(now + 10ms, [&order] { order.push_back(1); });
  queue.schedule(now + 30ms, [&order] { order.push_back(3); });

  EXPECT_EQ(queue.run_expired(now + 25ms), 2u);
  EXPECT_EQ(order, (std::vector<int>{1, 2}));

  EXPECT_EQ(queue.run_expired(now + 100ms), 1u);
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(TimerQueueTest, NextDeadlineTracksEarliestTimer) {
  TimerQueue queue;
  const auto now = TimerQueue::Clock::now();

  EXPECT_FALSE(queue.next_deadline());

  const auto first = queue.schedule(now + 50ms, [] {});
  const auto second = queue.schedule(now + 10ms, [] {});

  ASSERT_TRUE(queue.next_deadline());
  EXPECT_EQ(*queue.next_deadline(), now + 10ms);

  EXPECT_TRUE(queue.cancel(second.id));
  ASSERT_TRUE(queue.next_deadline());
  EXPECT_EQ(*queue.next_deadline(), now + 50ms);

  EXPECT_TRUE(queue.cancel(first.id));
  EXPECT_FALSE(queue.next_deadline());
}

TEST(TimerQueueTest, CancelPreventsCallback) {
  TimerQueue queue;
  std::atomic<int> calls{0};
  const auto result =
      queue.schedule(TimerQueue::Clock::now(), [&calls] { ++calls; });

  EXPECT_TRUE(queue.cancel(result.id));
  EXPECT_FALSE(queue.cancel(result.id));
  EXPECT_FALSE(queue.cancel(kInvalidTimerId));

  EXPECT_EQ(queue.run_expired(TimerQueue::Clock::now()), 0u);
  EXPECT_EQ(calls.load(), 0);
}

TEST(TimerQueueTest, ScheduleReportsWhetherDeadlineBecameEarlier) {
  TimerQueue queue;
  const auto now = TimerQueue::Clock::now();

  EXPECT_TRUE(queue.schedule(now + 100ms, [] {}).became_earlier);
  EXPECT_FALSE(queue.schedule(now + 200ms, [] {}).became_earlier);
  EXPECT_TRUE(queue.schedule(now + 50ms, [] {}).became_earlier);
  EXPECT_FALSE(queue.schedule(now + 50ms, [] {}).became_earlier);
}

TEST(TimerQueueTest, CallbacksRunWithoutHoldingQueueMutex) {
  TimerQueue queue;
  const auto now = TimerQueue::Clock::now();
  TimerId nested{kInvalidTimerId};

  queue.schedule(now, [&queue, &nested, now] {
    nested = queue.schedule(now + 10ms, [] {}).id;
    EXPECT_NE(nested, kInvalidTimerId);
    EXPECT_TRUE(queue.cancel(nested));
  });

  EXPECT_EQ(queue.run_expired(now), 1u);
  EXPECT_FALSE(queue.next_deadline());
}

} // namespace
