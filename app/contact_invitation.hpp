#pragma once

#include "app/trusted_contacts.hpp"
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

enum class ContactError {
  InvalidInvitation,
  CertificateError,
  IoError,
  InvalidImage,
  QrNotFound,
  AmbiguousQr,
  KeyConflict,
  SelfContact,
  InvalidContactsFile,
};

std::string_view contact_error_message(ContactError error);

struct ContactInvitation {
  std::string user_id;
  PublicKeyFingerprint fingerprint;

  std::expected<std::string, ContactError> encode() const;
  static std::expected<ContactInvitation, ContactError> parse(std::string_view text);
};

std::string fingerprint_hex(const PublicKeyFingerprint &fingerprint);

// Requires a certificate and its matching private key. Only public data is exported.
std::expected<ContactInvitation, ContactError>
make_contact_invitation(std::string user_id,
                        const std::filesystem::path &certificate,
                        const std::filesystem::path &private_key);
