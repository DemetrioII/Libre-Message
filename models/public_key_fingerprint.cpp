#include "models/public_key_fingerprint.hpp"
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <vector>

std::expected<PublicKeyFingerprint, FingerprintError>
certificate_fingerprint(X509 *certificate) {
  if (!certificate)
    return std::unexpected(FingerprintError::InvalidCertificate);
  EVP_PKEY *public_key = X509_get0_pubkey(certificate);
  if (!public_key)
    return std::unexpected(FingerprintError::InvalidCertificate);

  const int der_size = i2d_PUBKEY(public_key, nullptr);

  if (der_size <= 0)
    return std::unexpected(FingerprintError::EncodingFailed);

  std::vector<unsigned char> der(der_size);
  auto destination = der.data();

  if (i2d_PUBKEY(public_key, &destination) != der_size) {
    return std::unexpected(FingerprintError::EncodingFailed);
  }

  PublicKeyFingerprint fingerprint{};
  unsigned length = 0;

  if (EVP_Digest(der.data(), der.size(),
                 reinterpret_cast<unsigned char *>(fingerprint.data()), &length,
                 EVP_sha256(), nullptr) != 1 ||
      length != fingerprint.size())
    return std::unexpected(FingerprintError::DigestFailed);

  return fingerprint;
}
