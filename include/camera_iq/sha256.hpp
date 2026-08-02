#pragma once

#include <string>
#include <string_view>

namespace camera_iq {

// Returns the lowercase SHA-256 digest of every byte in `bytes`.
std::string sha256_hex(std::string_view bytes);

} // namespace camera_iq
