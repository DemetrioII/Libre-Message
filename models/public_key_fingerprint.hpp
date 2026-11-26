#pragma once
#include <array>
#include <cstddef>
#include <expected>
#include <openssl/types.h>

// SHA-256 digest of the DER-encoded SubjectPublicKeyInfo, not the certificate.
using PublicKeyFingerprint = std::array<std::byte, 32>;

enum class FingerprintError {
  InvalidCertificate,
  EncodingFailed,
  DigestFailed,
};

std::expected<PublicKeyFingerprint, FingerprintError>
certificate_fingerprint(X509 *certificate);
