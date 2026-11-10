#include "app/connection_manager.hpp"

#include <gtest/gtest.h>

namespace {

constexpr ConnectionId kIdA = 10;
constexpr ConnectionId kIdB = 11;

} // namespace

TEST(ConnectionManagerTest, RegisterThenFind) {
  ConnectionManager manager;

  manager.register_connection("recipient-a", kIdA);

  const auto id = manager.find_id("recipient-a");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, kIdA);
}

TEST(ConnectionManagerTest, FindUnknownReturnsNullopt) {
  ConnectionManager manager;

  EXPECT_EQ(manager.find_id("unknown"), std::nullopt);
}

TEST(ConnectionManagerTest, UnregisterByRecipientId) {
  ConnectionManager manager;
  manager.register_connection("recipient-a", kIdA);

  manager.unregister("recipient-a");

  EXPECT_EQ(manager.find_id("recipient-a"), std::nullopt);
}

TEST(ConnectionManagerTest, UnregisterByConnectionId) {
  ConnectionManager manager;
  manager.register_connection("recipient-a", kIdA);
  manager.register_connection("recipient-b", kIdB);

  manager.unregister_by_id(kIdA);

  EXPECT_EQ(manager.find_id("recipient-a"), std::nullopt);
  const auto id = manager.find_id("recipient-b");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, kIdB);
}

TEST(ConnectionManagerTest, ReRegisterSameRecipientReplacesConnectionId) {
  ConnectionManager manager;
  manager.register_connection("recipient-a", kIdA);

  manager.register_connection("recipient-a", kIdB);

  const auto id = manager.find_id("recipient-a");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, kIdB);
}

TEST(ConnectionManagerTest, UnregisterUnknownIsNoOp) {
  ConnectionManager manager;
  manager.register_connection("recipient-a", kIdA);

  manager.unregister("unknown");

  const auto id = manager.find_id("recipient-a");
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, kIdA);
}
