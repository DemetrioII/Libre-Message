#pragma once

#include "app/contact_invitation.hpp"

std::expected<void, ContactError>
write_contact_qr(const ContactInvitation &invitation,
                 const std::filesystem::path &png);

// PNG only, at most 16 MiB compressed and 4096x4096 pixels.
// Rejects images containing more than one detected QR code.
std::expected<ContactInvitation, ContactError>
read_contact_qr(const std::filesystem::path &png);
