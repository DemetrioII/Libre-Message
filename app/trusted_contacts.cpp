#include "app/trusted_contacts.hpp"
#include "app/session.hpp"
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <unistd.h>
#include <utility>

std::expected<void, TrustedContactsError>
TrustedContacts::add(std::string user_id,
                     const PublicKeyFingerprint &fingerprint) {
  if (user_id.size() != SessionInfo::kSessionIdSize)
    return std::unexpected(TrustedContactsError::InvalidUserId);

  for (char c : user_id) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
      return std::unexpected(TrustedContactsError::InvalidUserId);
  }

  std::lock_guard<std::mutex> lock{mutex_};
  const auto [it, inserted] =
      contacts_.try_emplace(std::move(user_id), fingerprint);
  if (!inserted && it->second != fingerprint)
    return std::unexpected(TrustedContactsError::KeyConflict);

  return {};
}

std::optional<PublicKeyFingerprint>
TrustedContacts::find(std::string_view user_id) const {
  std::lock_guard<std::mutex> lock{mutex_};
  const auto it = contacts_.find(std::string{user_id});
  if (it == contacts_.end())
    return std::nullopt;
  return it->second;
}

bool TrustedContacts::remove(std::string_view user_id) {
  std::lock_guard<std::mutex> lock{mutex_};
  return contacts_.erase(std::string{user_id}) != 0;
}

std::expected<void, TrustedContactsError>
TrustedContacts::load(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::unexpected(TrustedContactsError::IoError);

  TrustedContacts loaded;
  std::string line;
  while (std::getline(input, line)) {
    constexpr auto id_size = SessionInfo::kSessionIdSize;
    if (line.size() != id_size + 1 + 64 || line[id_size] != ' ')
      return std::unexpected(TrustedContactsError::InvalidData);

    PublicKeyFingerprint fingerprint{};
    for (std::size_t i = 0; i < fingerprint.size(); ++i) {
      unsigned value = 0;
      for (std::size_t j = 0; j < 2; ++j) {
        const char c = line[id_size + 1 + 2 * i + j];
        unsigned digit;
        if (c >= '0' && c <= '9')
          digit = c - '0';
        else if (c >= 'a' && c <= 'f')
          digit = c - 'a' + 10;
        else
          return std::unexpected(TrustedContactsError::InvalidData);
        value = value * 16 + digit;
      }
      fingerprint[i] = static_cast<std::byte>(value);
    }

    const auto id = line.substr(0, id_size);
    if (loaded.find(id) || !loaded.add(id, fingerprint))
      return std::unexpected(TrustedContactsError::InvalidData);
  }
  if (input.bad() || !input.eof())
    return std::unexpected(TrustedContactsError::IoError);

  std::lock_guard<std::mutex> lock{mutex_};
  contacts_.swap(loaded.contacts_);
  return {};
}

std::expected<void, TrustedContactsError>
TrustedContacts::save(const std::filesystem::path &path) const {
  static constexpr char hex[] = "0123456789abcdef";
  std::string contents;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    for (const auto &[id, fingerprint] : contacts_) {
      contents += id;
      contents += ' ';
      for (std::byte b : fingerprint) {
        const auto value = std::to_integer<unsigned>(b);
        contents += hex[value >> 4];
        contents += hex[value & 15];
      }
      contents += '\n';
    }
  }

  // The temporary file must be on the same filesystem as the destination.
  std::string temporary = path.string() + ".tmp.XXXXXX";
  const int fd = ::mkstemp(temporary.data());
  if (fd < 0)
    return std::unexpected(TrustedContactsError::IoError);

  bool success = true;
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const auto written =
        ::write(fd, contents.data() + offset, contents.size() - offset);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0) {
      success = false;
      break;
    }
    offset += static_cast<std::size_t>(written);
  }
  if (success && ::fsync(fd) != 0)
    success = false;
  if (::close(fd) != 0)
    success = false;
  if (success && ::rename(temporary.c_str(), path.c_str()) == 0)
    return {};

  ::unlink(temporary.c_str());
  return std::unexpected(TrustedContactsError::IoError);
}
