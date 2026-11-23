#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// SHA-256 digest of the DER-encoded SubjectPublicKeyInfo, not the certificate.
using PublicKeyFingerprint = std::array<std::byte, 32>;

enum class TrustedContactsError {
  InvalidUserId,
  KeyConflict,
  InvalidData,
  IoError,
};

class TrustedContacts {
  mutable std::mutex mutex_;
  std::unordered_map<std::string, PublicKeyFingerprint> contacts_;

public:
  // Accepts the 16 lowercase hexadecimal characters used by SessionInfo.
  // Adding the same ID and key again succeeds. A different key for an existing
  // ID returns KeyConflict and leaves the trusted key unchanged.
  std::expected<void, TrustedContactsError>
  add(std::string user_id, const PublicKeyFingerprint &fingerprint);

  // Returns a copy so callers do not retain references into the shared map.
  std::optional<PublicKeyFingerprint> find(std::string_view user_id) const;

  // Returns true only when a contact was removed.
  bool remove(std::string_view user_id);

  // Replaces the in-memory contacts only after the entire file is validated.
  // A missing file is an IoError; duplicate IDs are InvalidData.
  std::expected<void, TrustedContactsError>
  load(const std::filesystem::path &path);

  // Writes a snapshot using a private temporary file and atomic rename.
  // Format: one "<user_id> <64 lowercase hex fingerprint digits>" per line.
  std::expected<void, TrustedContactsError>
  save(const std::filesystem::path &path) const;
};
