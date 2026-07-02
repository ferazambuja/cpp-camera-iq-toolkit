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
  double black_level = 0;  // mean effective black across the four positions
  // Effective black for the top-left 2x2 mosaic positions (0,0)(0,1)(1,0)(1,1),
  // aligned with `cfa_pattern`. Combines LibRaw's scalar `black`, per-channel
  // `cblack[0..3]`, and the repeating `cblack[6..]` tile. Many sensors (the
  // Fuji X-T100 among them) park the real pedestal in the tile and leave the
  // scalar `black` and cblack[0..3] at 0 — so reading the scalar alone yields
  // a silent 0. See effective_black_levels().
  std::array<double, 4> black_per_channel{0, 0, 0, 0};
  double white_level = 0;  // sensor saturation ("maximum" in LibRaw terms)
};

// Builds the 2x2 CFA pattern string from LibRaw's color descriptor
// (`idata.cdesc`, e.g. "RGBG") and the color indices of the four positions
// (0,0) (0,1) (1,0) (1,1) as returned by LibRaw's COLOR().
// Returns empty string if any index is out of range.
std::string cfa_pattern_string(std::string_view cdesc,
                               const std::array<int, 4>& color_indices);

// Computes the effective black level for the four top-left mosaic positions
// (0,0)(0,1)(1,0)(1,1) from LibRaw's additive black representation:
//   effective = black + cblack[color] + cblack[6 + (r % bh)*bw + (c % bw)]
// where bh = cblack[4], bw = cblack[5] describe a repeating tile stored from
// cblack[6] onward. `color_at_position` holds the COLOR() channel index (0..3)
// for each of the four positions. `cblack_len` bounds every array access, so an
// out-of-range tile index simply contributes no tile term (never reads OOB).
//
// LIMITATION: the tile phase (r,c) is taken relative to the visible-area origin.
// That is exact only when the visible area starts at the raw origin — i.e. zero
// crop margin (validated on the Fuji X-T100, top/left margin 0). For a sensor
// whose visible area is offset inside the raw frame, the tile index must be
// shifted by the raw (top_margin,left_margin). Until validated against such a
// sensor, treat non-zero-margin RAWs as UNVERIFIED for this helper.
std::array<double, 4> effective_black_levels(
    unsigned black, const unsigned* cblack, std::size_t cblack_len,
    const std::array<int, 4>& color_at_position);

// Reads metadata from a RAW file. Returns std::nullopt if the file does not
// exist or LibRaw cannot parse it. Never throws.
std::optional<RawMeta> read_raw_metadata(const std::filesystem::path& raw);

}  // namespace camera_iq
