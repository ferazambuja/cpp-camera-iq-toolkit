#include "camera_iq/filename_meta.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace camera_iq {
namespace {

bool iequals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool is_raw_extension(std::string_view ext) {
  return iequals(ext, "raf") || iequals(ext, "nef") || iequals(ext, "arw") ||
         iequals(ext, "cr2") || iequals(ext, "iiq") || iequals(ext, "dng");
}

bool all_digits(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  }
  return true;
}

std::optional<double> to_double(std::string_view s) {
  try {
    size_t consumed = 0;
    const double v = std::stod(std::string(s), &consumed);
    if (consumed != s.size()) return std::nullopt;
    return v;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<int> to_int(std::string_view s) {
  if (!all_digits(s)) return std::nullopt;
  try {
    return std::stoi(std::string(s));
  } catch (...) {
    return std::nullopt;
  }
}

std::vector<std::string_view> split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  size_t start = 0;
  while (start <= s.size()) {
    const size_t pos = s.find(sep, start);
    if (pos == std::string_view::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return out;
}

// "f9.0" -> 9.0
std::optional<double> parse_aperture_token(std::string_view tok) {
  if (tok.size() < 2 || tok[0] != 'f') return std::nullopt;
  if (!std::isdigit(static_cast<unsigned char>(tok[1]))) return std::nullopt;
  return to_double(tok.substr(1));
}

// "1:100" or "s1-40" -> seconds. ':' and '-' stand in for '/' because '/' is
// not a legal filename character; the Nikon OECF archive prefixes shutter with s.
std::optional<double> parse_shutter_token(std::string_view tok) {
  std::string_view body = tok;
  if (body.size() >= 2 && body[0] == 's' &&
      std::isdigit(static_cast<unsigned char>(body[1]))) {
    body.remove_prefix(1);
  }
  size_t sep = body.find(':');
  if (sep == std::string_view::npos) sep = body.find('-');
  if (sep == std::string_view::npos) return std::nullopt;
  const auto num = to_double(body.substr(0, sep));
  const auto den = to_double(body.substr(sep + 1));
  if (!num || !den || *den == 0.0) return std::nullopt;
  return *num / *den;
}

// "ISO200" or "i100" -> integer ISO.
std::optional<int> parse_iso_token(std::string_view tok) {
  if (tok.size() >= 4 && tok.substr(0, 3) == "ISO") return to_int(tok.substr(3));
  if (tok.size() >= 2 && tok[0] == 'i' &&
      std::isdigit(static_cast<unsigned char>(tok[1]))) {
    return to_int(tok.substr(1));
  }
  return std::nullopt;
}

// "DSCF0299" -> 299
std::optional<int> parse_frame_token(std::string_view tok) {
  if (tok.size() < 5 || !iequals(tok.substr(0, 4), "DSCF")) return std::nullopt;
  return to_int(tok.substr(4));
}

}  // namespace

FilenameMeta parse_capture_filename(std::string_view filename) {
  FilenameMeta meta;

  const size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos) return meta;
  if (!is_raw_extension(filename.substr(dot + 1))) return meta;

  const std::string_view stem = filename.substr(0, dot);
  const auto tokens = split(stem, '_');

  size_t first_exposure_token = tokens.size();
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string_view tok = tokens[i];
    if (!meta.aperture) {
      if (const auto ap = parse_aperture_token(tok)) {
        meta.aperture = ap;
        first_exposure_token = std::min(first_exposure_token, i);
        continue;
      }
    }
    if (!meta.shutter_s) {
      if (const auto sh = parse_shutter_token(tok)) {
        meta.shutter_s = sh;
        meta.shutter_str = std::string(tok);
        first_exposure_token = std::min(first_exposure_token, i);
        continue;
      }
    }
    if (!meta.iso) {
      if (const auto iso = parse_iso_token(tok)) {
        meta.iso = iso;
        first_exposure_token = std::min(first_exposure_token, i);
        continue;
      }
    }
    if (!meta.frame) {
      if (const auto fr = parse_frame_token(tok)) {
        meta.frame = fr;
        first_exposure_token = std::min(first_exposure_token, i);
        continue;
      }
      if (first_exposure_token < i) {
        if (const auto fr = to_int(tok)) {
          meta.frame = fr;
          continue;
        }
      }
    }
  }

  if (first_exposure_token > 0 && first_exposure_token < tokens.size()) {
    std::string group;
    for (size_t i = 0; i < first_exposure_token; ++i) {
      if (i > 0) group += '_';
      group += std::string(tokens[i]);
    }
    if (!group.empty()) meta.group = std::move(group);
  }

  return meta;
}

}  // namespace camera_iq
