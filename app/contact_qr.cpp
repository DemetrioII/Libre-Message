#include "app/contact_qr.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>
#include <qrcodegen.hpp>
#include <quirc.h>

#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

std::expected<void, ContactError>
write_contact_qr(const ContactInvitation &invitation,
                 const std::filesystem::path &png) {
  const auto text = invitation.encode();
  if (!text)
    return std::unexpected(text.error());
  const auto code = qrcodegen::QrCode::encodeText(
      text->c_str(), qrcodegen::QrCode::Ecc::QUARTILE);
  constexpr int border = 4;
  constexpr int scale = 8;
  const int size = (code.getSize() + 2 * border) * scale;
  std::vector<unsigned char> pixels(size * size, 255);
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      if (code.getModule(x / scale - border, y / scale - border))
        pixels[y * size + x] = 0;
  if (!stbi_write_png(png.string().c_str(), size, size, 1, pixels.data(), size))
    return std::unexpected(ContactError::IoError);
  return {};
}

std::expected<ContactInvitation, ContactError>
read_contact_qr(const std::filesystem::path &png) {
  constexpr std::size_t max_file_size = 16 * 1024 * 1024;
  std::ifstream input(png, std::ios::binary);
  if (!input)
    return std::unexpected(ContactError::IoError);
  // Read with a hard limit even for special files and files changing size.
  std::vector<unsigned char> bytes;
  char chunk[8192];
  while (input.read(chunk, sizeof(chunk)) || input.gcount() > 0) {
    if (bytes.size() + input.gcount() > max_file_size)
      return std::unexpected(ContactError::InvalidImage);
    bytes.insert(bytes.end(), chunk, chunk + input.gcount());
  }
  if (input.bad())
    return std::unexpected(ContactError::IoError);
  int width = 0, height = 0, channels = 0;
  if (!stbi_info_from_memory(bytes.data(), bytes.size(), &width, &height, &channels) ||
      width <= 0 || height <= 0 || width > 4096 || height > 4096)
    return std::unexpected(ContactError::InvalidImage);
  std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
      stbi_load_from_memory(bytes.data(), bytes.size(), &width, &height, &channels, 1),
      stbi_image_free);
  if (!pixels)
    return std::unexpected(ContactError::InvalidImage);
  std::unique_ptr<quirc, decltype(&quirc_destroy)> decoder(quirc_new(), quirc_destroy);
  if (!decoder || quirc_resize(decoder.get(), width, height) != 0)
    return std::unexpected(ContactError::InvalidImage);
  auto image = quirc_begin(decoder.get(), nullptr, nullptr);
  std::memcpy(image, pixels.get(), static_cast<std::size_t>(width) * height);
  quirc_end(decoder.get());
  const int count = quirc_count(decoder.get());
  if (count == 0)
    return std::unexpected(ContactError::QrNotFound);
  if (count != 1)
    return std::unexpected(ContactError::AmbiguousQr);
  quirc_code code{};
  quirc_data data{};
  quirc_extract(decoder.get(), 0, &code);
  if (quirc_decode(&code, &data) != QUIRC_SUCCESS) {
    quirc_flip(&code);
    if (quirc_decode(&code, &data) != QUIRC_SUCCESS)
      return std::unexpected(ContactError::QrNotFound);
  }
  return ContactInvitation::parse(std::string_view(
      reinterpret_cast<const char *>(data.payload), data.payload_len));
}
