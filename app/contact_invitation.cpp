#include "app/contact_invitation.hpp"
#include "app/session.hpp"
#include <memory>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <utility>
#include <vector>

static constexpr std::string_view kContactPrefix = "libremessage://contact?v=1&id=";
static constexpr std::string_view kFingerprintField = "&spki_sha256=";

static bool valid_contact_id(std::string_view id) {
  if (id.size() != SessionInfo::kSessionIdSize)
    return false;
  for (char c : id)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return false;
  return true;
}

std::string_view contact_error_message(ContactError error) {
  switch (error) {
  case ContactError::InvalidInvitation: return "Invalid contact invitation or unsupported version";
  case ContactError::CertificateError: return "Cannot read certificate and matching private key";
  case ContactError::IoError: return "Cannot read or write file";
  case ContactError::InvalidImage: return "Invalid PNG or image exceeds size limit";
  case ContactError::QrNotFound: return "No readable QR code found";
  case ContactError::AmbiguousQr: return "Image must contain exactly one QR code";
  case ContactError::KeyConflict: return "Contact already has a different trusted key";
  case ContactError::SelfContact: return "Cannot import your own contact ID";
  case ContactError::InvalidContactsFile: return "Cannot load trusted contacts file";
  }
  return "Unknown contact error";
}

std::string fingerprint_hex(const PublicKeyFingerprint &fingerprint) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (std::byte b : fingerprint) {
    const auto value = std::to_integer<unsigned>(b);
    result += digits[value >> 4];
    result += digits[value & 15];
  }
  return result;
}

std::expected<std::string, ContactError> ContactInvitation::encode() const {
  if (!valid_contact_id(user_id))
    return std::unexpected(ContactError::InvalidInvitation);
  return std::string(kContactPrefix) + user_id + std::string(kFingerprintField) +
         fingerprint_hex(fingerprint);
}

std::expected<ContactInvitation, ContactError>
ContactInvitation::parse(std::string_view text) {
  constexpr auto id_size = SessionInfo::kSessionIdSize;
  constexpr auto size = kContactPrefix.size() + id_size + kFingerprintField.size() + 64;
  if (text.size() != size || !text.starts_with(kContactPrefix))
    return std::unexpected(ContactError::InvalidInvitation);
  text.remove_prefix(kContactPrefix.size());
  const auto id = text.substr(0, id_size);
  if (!valid_contact_id(id))
    return std::unexpected(ContactError::InvalidInvitation);
  text.remove_prefix(id_size);
  if (!text.starts_with(kFingerprintField))
    return std::unexpected(ContactError::InvalidInvitation);
  text.remove_prefix(kFingerprintField.size());
  PublicKeyFingerprint fingerprint{};
  for (std::size_t i = 0; i < fingerprint.size(); ++i) {
    unsigned value = 0;
    for (std::size_t j = 0; j < 2; ++j) {
      const char c = text[2 * i + j];
      if (c >= '0' && c <= '9')
        value = value * 16 + c - '0';
      else if (c >= 'a' && c <= 'f')
        value = value * 16 + c - 'a' + 10;
      else
        return std::unexpected(ContactError::InvalidInvitation);
    }
    fingerprint[i] = static_cast<std::byte>(value);
  }
  return ContactInvitation{std::string(id), fingerprint};
}

std::expected<ContactInvitation, ContactError>
make_contact_invitation(std::string user_id,
                        const std::filesystem::path &certificate,
                        const std::filesystem::path &private_key) {
  if (!valid_contact_id(user_id))
    return std::unexpected(ContactError::InvalidInvitation);
  const auto cert_name = certificate.string();
  const auto key_name = private_key.string();
  std::unique_ptr<BIO, decltype(&BIO_free)> cert_bio(
      BIO_new_file(cert_name.c_str(), "r"), BIO_free);
  std::unique_ptr<BIO, decltype(&BIO_free)> key_bio(
      BIO_new_file(key_name.c_str(), "r"), BIO_free);
  if (!cert_bio || !key_bio)
    return std::unexpected(ContactError::CertificateError);
  std::unique_ptr<X509, decltype(&X509_free)> cert(
      PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr), X509_free);
  // An explicit empty password avoids interactive OpenSSL password prompts.
  char password[] = "";
  std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
      PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, password), EVP_PKEY_free);
  if (!cert || !key || X509_check_private_key(cert.get(), key.get()) != 1)
    return std::unexpected(ContactError::CertificateError);
  EVP_PKEY *public_key = X509_get0_pubkey(cert.get());
  const int der_size = public_key ? i2d_PUBKEY(public_key, nullptr) : -1;
  if (der_size <= 0)
    return std::unexpected(ContactError::CertificateError);
  std::vector<unsigned char> der(der_size);
  auto destination = der.data();
  if (i2d_PUBKEY(public_key, &destination) != der_size)
    return std::unexpected(ContactError::CertificateError);
  PublicKeyFingerprint fingerprint{};
  unsigned length = 0;
  if (EVP_Digest(der.data(), der.size(),
                 reinterpret_cast<unsigned char *>(fingerprint.data()), &length,
                 EVP_sha256(), nullptr) != 1 || length != fingerprint.size())
    return std::unexpected(ContactError::CertificateError);
  return ContactInvitation{std::move(user_id), fingerprint};
}
