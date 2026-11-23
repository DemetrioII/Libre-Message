#include "app/trusted_contacts.hpp"

#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr char kUserA[] = "0123456789abcdef";
constexpr char kUserB[] = "fedcba9876543210";

PublicKeyFingerprint make_fingerprint(std::byte value) {
  PublicKeyFingerprint fingerprint;
  fingerprint.fill(value);
  return fingerprint;
}

TEST(TrustedContactsTest, FindsAddedContactsAndReturnsNulloptForUnknownId) {
  TrustedContacts contacts;
  const auto fingerprint_a = make_fingerprint(std::byte{0x12});
  const auto fingerprint_b = make_fingerprint(std::byte{0x34});

  EXPECT_EQ(contacts.find(kUserA), std::nullopt);
  ASSERT_TRUE(contacts.add(kUserA, fingerprint_a).has_value());
  ASSERT_TRUE(contacts.add(kUserB, fingerprint_b).has_value());

  EXPECT_EQ(contacts.find(kUserA), fingerprint_a);
  EXPECT_EQ(contacts.find(kUserB), fingerprint_b);
}

TEST(TrustedContactsTest, AddingSameKeyAgainSucceeds) {
  TrustedContacts contacts;
  const auto fingerprint = make_fingerprint(std::byte{0x12});
  ASSERT_TRUE(contacts.add(kUserA, fingerprint).has_value());

  EXPECT_TRUE(contacts.add(kUserA, fingerprint).has_value());
  EXPECT_EQ(contacts.find(kUserA), fingerprint);
}

TEST(TrustedContactsTest, ConflictingKeyIsRejectedAndOriginalKeyIsPreserved) {
  TrustedContacts contacts;
  const auto original = make_fingerprint(std::byte{0x12});
  const auto replacement = make_fingerprint(std::byte{0x34});
  ASSERT_TRUE(contacts.add(kUserA, original).has_value());

  const auto result = contacts.add(kUserA, replacement);

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), TrustedContactsError::KeyConflict);
  EXPECT_EQ(contacts.find(kUserA), original);
}

TEST(TrustedContactsTest, InvalidUserIdsAreRejectedWithoutAddingContact) {
  TrustedContacts contacts;
  const auto fingerprint = make_fingerprint(std::byte{0x12});
  const std::vector<std::string> invalid_ids = {
      "", "0123456789abcde", "0123456789abcdef0", "0123456789abcdeF",
      "0123456789abcdeg", "0123456789abcde ",
      std::string("0123456789abcde\0", 16),
      std::string(16, static_cast<char>(0xff))};
  ASSERT_TRUE(contacts.add(kUserA, fingerprint).has_value());

  for (const auto &id : invalid_ids) {
    SCOPED_TRACE(::testing::PrintToString(id));
    const auto result = contacts.add(id, fingerprint);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TrustedContactsError::InvalidUserId);
    EXPECT_EQ(contacts.find(id), std::nullopt);
  }

  EXPECT_EQ(contacts.find(kUserA), fingerprint);
}

TEST(TrustedContactsTest, RemovingContactPreservesOtherContacts) {
  TrustedContacts contacts;
  const auto fingerprint_a = make_fingerprint(std::byte{0x12});
  const auto fingerprint_b = make_fingerprint(std::byte{0x34});
  ASSERT_TRUE(contacts.add(kUserA, fingerprint_a).has_value());
  ASSERT_TRUE(contacts.add(kUserB, fingerprint_b).has_value());

  EXPECT_TRUE(contacts.remove(kUserA));
  EXPECT_EQ(contacts.find(kUserA), std::nullopt);
  EXPECT_EQ(contacts.find(kUserB), fingerprint_b);
  EXPECT_FALSE(contacts.remove(kUserA));
  EXPECT_FALSE(contacts.remove("unknown"));
  EXPECT_EQ(contacts.find(kUserB), fingerprint_b);
}

class TrustedContactsPersistenceTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto pattern = (std::filesystem::temp_directory_path() /
                    "trusted-contacts-test-XXXXXX").string();
    const auto directory = ::mkdtemp(pattern.data());
    ASSERT_NE(directory, nullptr);
    directory_ = directory;
    path_ = directory_ / "contacts";
  }

  void TearDown() override {
    if (!directory_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(directory_, error);
      EXPECT_FALSE(error) << error.message();
    }
  }

  std::filesystem::path directory_;
  std::filesystem::path path_;
};

TEST_F(TrustedContactsPersistenceTest, RoundTripPreservesEveryFingerprintByte) {
  TrustedContacts source;
  PublicKeyFingerprint fingerprint{};
  for (std::size_t i = 0; i < fingerprint.size(); ++i)
    fingerprint[i] = static_cast<std::byte>(i * 8 + 7);
  ASSERT_TRUE(source.add(kUserA, fingerprint));
  ASSERT_TRUE(source.add(kUserB, make_fingerprint(std::byte{0}))); 
  ASSERT_TRUE(source.save(path_));

  TrustedContacts restored;
  ASSERT_TRUE(restored.load(path_));
  EXPECT_EQ(restored.find(kUserA), fingerprint);
  EXPECT_EQ(restored.find(kUserB), make_fingerprint(std::byte{0}));
  const auto conflict = restored.add(kUserA, make_fingerprint(std::byte{0xff}));
  ASSERT_FALSE(conflict);
  EXPECT_EQ(conflict.error(), TrustedContactsError::KeyConflict);
}

TEST_F(TrustedContactsPersistenceTest, SaveReplacesExistingFileIncludingEmptyStore) {
  TrustedContacts contacts;
  ASSERT_TRUE(contacts.add(kUserA, make_fingerprint(std::byte{0xff})));
  ASSERT_TRUE(contacts.save(path_));
  ASSERT_TRUE(contacts.remove(kUserA));
  ASSERT_TRUE(contacts.save(path_));

  TrustedContacts restored;
  ASSERT_TRUE(restored.add(kUserB, make_fingerprint(std::byte{0x12})));
  ASSERT_TRUE(restored.load(path_));
  EXPECT_EQ(restored.find(kUserA), std::nullopt);
  EXPECT_EQ(restored.find(kUserB), std::nullopt);
  EXPECT_TRUE(std::filesystem::is_empty(path_));
}

TEST_F(TrustedContactsPersistenceTest, CorruptFilesNeverPartiallyReplaceContacts) {
  const std::string valid = std::string(kUserA) + " " + std::string(64, 'a');
  const std::vector<std::string> malformed = {
      "\n", "short\n", std::string(kUserA) + " " + std::string(63, 'a'),
      std::string(kUserA) + " " + std::string(64, 'g'),
      "0123456789abcdeF " + std::string(64, 'a'), valid + " extra",
      valid + "\n" + valid + "\n",
      valid + "\n" + std::string(kUserA) + " " + std::string(64, 'b')};

  for (const auto &bad : malformed) {
    SCOPED_TRACE(bad);
    std::ofstream output(path_, std::ios::binary);
    output << valid << '\n' << bad;
    output.close();
    ASSERT_TRUE(output);

    TrustedContacts contacts;
    const auto original = make_fingerprint(std::byte{0x12});
    ASSERT_TRUE(contacts.add(kUserB, original));
    const auto result = contacts.load(path_);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), TrustedContactsError::InvalidData);
    EXPECT_EQ(contacts.find(kUserA), std::nullopt);
    EXPECT_EQ(contacts.find(kUserB), original);
  }
}

TEST_F(TrustedContactsPersistenceTest, MissingFileReturnsIoErrorAndPreservesContacts) {
  TrustedContacts contacts;
  const auto fingerprint = make_fingerprint(std::byte{0x12});
  ASSERT_TRUE(contacts.add(kUserA, fingerprint));
  const auto result = contacts.load(path_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), TrustedContactsError::IoError);
  EXPECT_EQ(contacts.find(kUserA), fingerprint);
}

TEST_F(TrustedContactsPersistenceTest, FailedSaveCleansUpTemporaryFile) {
  ASSERT_TRUE(std::filesystem::create_directory(path_));
  TrustedContacts contacts;
  ASSERT_TRUE(contacts.add(kUserA, make_fingerprint(std::byte{0x12})));
  const auto result = contacts.save(path_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), TrustedContactsError::IoError);
  EXPECT_TRUE(std::filesystem::is_directory(path_));
  std::size_t entries = 0;
  for (const auto &entry : std::filesystem::directory_iterator(directory_)) {
    EXPECT_EQ(entry.path(), path_);
    ++entries;
  }
  EXPECT_EQ(entries, 1u);
}

} // namespace
