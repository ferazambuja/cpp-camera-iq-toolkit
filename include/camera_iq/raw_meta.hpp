#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace camera_iq {

// Metadata extracted from a RAW file via LibRaw. Exposure fields mirror the
// EXIF values so they can be cross-checked against filename metadata.
struct RawMeta {
  std::string make;
  std::string model;
  double iso = 0;
  double shutter_s = 0;
  double aperture = 0;
  double focal_length_mm = 0;
  std::string timestamp;    // "YYYY-MM-DD HH:MM:SS" local camera time
  std::string cfa_pattern;  // top-left 2x2, e.g. "RGGB" — derived, never hardcoded
  int raw_width = 0;
  int raw_height = 0;
  double black_level = 0;
  // Per-channel black (LibRaw cblack[0..3]); Fuji RAFs often store black here
  // rather than in the scalar `black`. Order follows LibRaw channel indexing.
  std::array<double, 4> black_per_channel{0, 0, 0, 0};
  double white_level = 0;  // sensor saturation ("maximum" in LibRaw terms)
};

// Builds the 2x2 CFA pattern string from LibRaw's color descriptor
// (`idata.cdesc`, e.g. "RGBG") and the color indices of the four positions
// (0,0) (0,1) (1,0) (1,1) as returned by LibRaw's COLOR().
// Returns empty string if any index is out of range.
std::string cfa_pattern_string(std::string_view cdesc,
                               const std::array<int, 4>& color_indices);

// Reads metadata from a RAW file. Returns std::nullopt if the file does not
// exist or LibRaw cannot parse it. Never throws.
std::optional<RawMeta> read_raw_metadata(const std::filesystem::path& raw);

}  // namespace camera_iq
