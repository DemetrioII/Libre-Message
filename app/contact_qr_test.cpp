#include "app/contact_qr.hpp"

#include <cstdlib>
#include <fstream>
#include <memory>
#include <vector>
#include <gtest/gtest.h>
#include <openssl/pem.h>
#include <qrcodegen.hpp>
#include <stb_image_write.h>

namespace {

constexpr char kId[] = "0123456789abcdef";

ContactInvitation invitation() {
  ContactInvitation result{kId, {}};
  for (std::size_t i = 0; i < result.fingerprint.size(); ++i)
    result.fingerprint[i] = static_cast<std::byte>(i * 8 + 7);
  return result;
}

TEST(ContactInvitationTest, CanonicalTextRoundTrip) {
  const auto original = invitation();
  const auto text = original.encode();
  ASSERT_TRUE(text);
  EXPECT_EQ(*text,
      "libremessage://contact?v=1&id=0123456789abcdef&spki_sha256="
      "070f171f272f373f474f575f676f777f878f979fa7afb7bfc7cfd7dfe7eff7ff");
  const auto parsed = ContactInvitation::parse(*text);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->user_id, original.user_id);
  EXPECT_EQ(parsed->fingerprint, original.fingerprint);
}

TEST(ContactInvitationTest, RejectsMalformedAndNoncanonicalInvitations) {
  const auto original = *invitation().encode();
  std::vector<std::string> bad = {
      "", "https://example.com", original + "&extra=1", original + "\n",
      original.substr(0, original.size() - 1),
      "libremessage://contact?v=2&id=0123456789abcdef&spki_sha256=" + std::string(64, 'a'),
      "libremessage://contact?v=1&id=0123456789abcdeF&spki_sha256=" + std::string(64, 'a'),
      "libremessage://contact?v=1&id=0123456789abcdef&spki_sha256=" + std::string(64, 'G')};
  auto embedded_null = original;
  embedded_null.back() = '\0';
  bad.push_back(embedded_null);
  for (const auto &text : bad) {
    SCOPED_TRACE(::testing::PrintToString(text));
    const auto result = ContactInvitation::parse(text);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), ContactError::InvalidInvitation);
  }
  auto invalid = invitation();
  invalid.user_id = "invalid";
  EXPECT_FALSE(invalid.encode());
}

class ContactQrTest : public ::testing::Test {
protected:
  void SetUp() override {
    auto pattern = (std::filesystem::temp_directory_path() / "contact-qr-XXXXXX").string();
    const auto directory = ::mkdtemp(pattern.data());
    ASSERT_NE(directory, nullptr);
    directory_ = directory;
    png_ = directory_ / "contact with spaces.png";
  }

  void TearDown() override {
    if (!directory_.empty()) {
      std::error_code error;
      std::filesystem::remove_all(directory_, error);
      EXPECT_FALSE(error) << error.message();
    }
  }

  void write_codes(const std::string &text, int copies = 1) {
    const auto code = qrcodegen::QrCode::encodeText(text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
    const int side = (code.getSize() + 8) * 8;
    const int width = side * copies;
    std::vector<unsigned char> pixels(width * side, 255);
    for (int y = 0; y < side; ++y)
      for (int x = 0; x < width; ++x)
        if (code.getModule((x % side) / 8 - 4, y / 8 - 4))
          pixels[y * width + x] = 0;
    ASSERT_NE(stbi_write_png(png_.c_str(), width, side, 1, pixels.data(), width), 0);
  }

  std::filesystem::path directory_;
  std::filesystem::path png_;
};

TEST_F(ContactQrTest, ExportedPngCanBeImported) {
  const auto original = invitation();
  ASSERT_TRUE(write_contact_qr(original, png_));
  const auto imported = read_contact_qr(png_);
  ASSERT_TRUE(imported);
  EXPECT_EQ(imported->user_id, original.user_id);
  EXPECT_EQ(imported->fingerprint, original.fingerprint);
  std::ifstream input(png_, std::ios::binary);
  char signature[8]{};
  input.read(signature, sizeof(signature));
  EXPECT_EQ(std::string(signature, 8), std::string("\x89PNG\r\n\x1a\n", 8));
}

TEST_F(ContactQrTest, RejectsMultipleQrCodes) {
  write_codes(*invitation().encode(), 2);
  const auto result = read_contact_qr(png_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), ContactError::AmbiguousQr);
}

TEST_F(ContactQrTest, RejectsUnrelatedQrPayload) {
  write_codes("https://example.com");
  const auto result = read_contact_qr(png_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), ContactError::InvalidInvitation);
}

TEST_F(ContactQrTest, RejectsBlankPngAndOversizedDimensions) {
  std::vector<unsigned char> pixels(128 * 128, 255);
  ASSERT_NE(stbi_write_png(png_.c_str(), 128, 128, 1, pixels.data(), 128), 0);
  auto result = read_contact_qr(png_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), ContactError::QrNotFound);

  ASSERT_NE(stbi_write_png(png_.c_str(), 4097, 1, 1, pixels.data(), 4097), 0);
  result = read_contact_qr(png_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), ContactError::InvalidImage);
}

TEST_F(ContactQrTest, RejectsTruncatedPngAndOversizedFile) {
  ASSERT_TRUE(write_contact_qr(invitation(), png_));
  std::filesystem::resize_file(png_, 16);
  auto result = read_contact_qr(png_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), ContactError::InvalidImage);
  {
    std::ofstream output(png_, std::ios::binary);
    output.seekp(16 * 1024 * 1024);
    output.put('x');
  }
  result = read_contact_qr(png_);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), ContactError::InvalidImage);
}

TEST_F(ContactQrTest, FileErrorsAreReported) {
  const auto missing = read_contact_qr(png_);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error(), ContactError::IoError);
  const auto unwritable = write_contact_qr(invitation(), directory_ / "missing" / "qr.png");
  ASSERT_FALSE(unwritable);
  EXPECT_EQ(unwritable.error(), ContactError::IoError);
}

using TestKey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

TestKey generate_key() {
  return TestKey(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
}

bool write_identity(EVP_PKEY *key, const std::filesystem::path &cert_path,
                    const std::filesystem::path &key_path, long serial) {
  std::unique_ptr<X509, decltype(&X509_free)> cert(X509_new(), X509_free);
  if (!cert || X509_set_version(cert.get(), 2) != 1 ||
      ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial) != 1 ||
      !X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0) ||
      !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 3600) ||
      X509_set_pubkey(cert.get(), key) != 1)
    return false;
  auto name = X509_get_subject_name(cert.get());
  if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
          reinterpret_cast<const unsigned char *>("test"), -1, -1, 0) != 1 ||
      X509_set_issuer_name(cert.get(), name) != 1 ||
      X509_sign(cert.get(), key, EVP_sha256()) <= 0)
    return false;
  std::unique_ptr<BIO, decltype(&BIO_free)> cert_out(BIO_new_file(cert_path.c_str(), "w"), BIO_free);
  std::unique_ptr<BIO, decltype(&BIO_free)> key_out(BIO_new_file(key_path.c_str(), "w"), BIO_free);
  return cert_out && key_out && PEM_write_bio_X509(cert_out.get(), cert.get()) == 1 &&
      PEM_write_bio_PrivateKey(key_out.get(), key, nullptr, nullptr, 0, nullptr, nullptr) == 1;
}

TEST_F(ContactQrTest, FingerprintIsSpkiSha256AndSurvivesCertificateRenewal) {
  auto key = generate_key();
  ASSERT_TRUE(key);
  const auto cert_path = directory_ / "server.crt";
  const auto key_path = directory_ / "server.key";
  ASSERT_TRUE(write_identity(key.get(), cert_path, key_path, 1));
  const auto first = make_contact_invitation(kId, cert_path, key_path);
  ASSERT_TRUE(first);

  std::unique_ptr<BIO, decltype(&BIO_free)> der(BIO_new(BIO_s_mem()), BIO_free);
  ASSERT_TRUE(der);
  ASSERT_EQ(i2d_PUBKEY_bio(der.get(), key.get()), 1);
  char *data = nullptr;
  const auto size = BIO_get_mem_data(der.get(), &data);
  ASSERT_GT(size, 0);
  PublicKeyFingerprint expected{};
  unsigned length = 0;
  ASSERT_EQ(EVP_Digest(data, size, reinterpret_cast<unsigned char *>(expected.data()),
                       &length, EVP_sha256(), nullptr), 1);
  ASSERT_EQ(length, expected.size());
  EXPECT_EQ(first->fingerprint, expected);

  ASSERT_TRUE(write_identity(key.get(), cert_path, key_path, 2));
  const auto renewed = make_contact_invitation(kId, cert_path, key_path);
  ASSERT_TRUE(renewed);
  EXPECT_EQ(renewed->fingerprint, first->fingerprint);

  auto different = generate_key();
  ASSERT_TRUE(different);
  ASSERT_TRUE(write_identity(different.get(), directory_ / "other.crt", key_path, 3));
  const auto mismatch = make_contact_invitation(kId, cert_path, key_path);
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error(), ContactError::CertificateError);
}

TEST_F(ContactQrTest, MissingCertificateDoesNotProduceInvitation) {
  const auto result = make_contact_invitation(kId, directory_ / "none.crt", directory_ / "none.key");
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error(), ContactError::CertificateError);
}

} // namespace
