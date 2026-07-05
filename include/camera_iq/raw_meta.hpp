#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/roi.hpp"

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
  std::string timestamp;    // "YYYY-MM-DD HH:MM:SS" local camera-clock time
  std::string cfa_pattern;  // top-left 2x2, e.g. "RGGB" — derived, never hardcoded
  int raw_width = 0;
  int raw_height = 0;
  int visible_width = 0;   // LibRaw sizes.width: active/meaningful image area
  int visible_height = 0;  // LibRaw sizes.height: active/meaningful image area
  int top_margin = 0;     // active-area origin inside the raw frame
  int left_margin = 0;
  int raw_pitch_bytes = 0;  // row stride for rawdata.raw_image, in bytes
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

// Computes the effective black level for the four active-area top-left mosaic
// positions (0,0)(0,1)(1,0)(1,1) from LibRaw's additive black representation:
//   effective = black + cblack[color] +
//               cblack[6 + (r % bh)*bw + (c % bw)]
// where bh = cblack[4], bw = cblack[5] describe a repeating tile stored from
// cblack[6] onward. `color_at_position` holds the COLOR() channel index (0..3)
// for each of the four positions. `cblack_len` bounds every array access, so an
// out-of-range tile index simply contributes no tile term (never reads OOB).
//
// The cblack tile phase is active-area-local. This matches LibRaw's
// subtract_black/raw2image convention: raw access adds margins to locate active
// pixels, but black-tile row/column indices are visible-image coordinates.
std::array<double, 4> effective_black_levels(
    unsigned black, const unsigned* cblack, std::size_t cblack_len,
    const std::array<int, 4>& color_at_position);

// True only for ordinary 2x2 Bayer masks represented by LibRaw's filters bitmask.
// Special values below 1000 include full-color/monochrome, Leaf 16x16 and Fuji
// X-Trans; Evidence raw-stats intentionally rejects those layouts.
bool is_supported_bayer_filter(unsigned filters);

// Converts LibRaw raw_pitch bytes to uint16 pixel stride. Some unpackers leave
// raw_pitch unset; for tightly packed Bayer raw_image buffers, raw_width is then
// the effective stride. Returns 0 for invalid odd byte pitches.
int effective_raw_stride_pixels(unsigned raw_pitch_bytes, unsigned raw_width);

// Reads metadata available after LibRaw open_file(). Returns std::nullopt if the
// file does not exist or LibRaw cannot parse it. Never throws.
//
// Note: some makers finalize black level / pitch during unpack(); use
// read_raw_cfa_stats() when scientifically correct black subtraction is needed.
std::optional<RawMeta> read_raw_metadata(const std::filesystem::path& raw);

// RAW metadata plus per-CFA-channel statistics over the black-subtracted mosaic.
struct RawCfaReport {
  RawMeta meta;
  std::optional<RoiRect> measurement_roi;
  std::array<ChannelStats, 4> planes;
};

// Active, black-subtracted Bayer mosaic copied out of LibRaw after unpack().
// `samples` is row-major with `row_stride_pixels == width`; values are signed
// residuals and may be below zero.
struct RawCfaImage {
  RawMeta meta;
  int width = 0;
  int height = 0;
  int row_stride_pixels = 0;
  std::array<int, 4> color_at_position{0, 1, 2, 3};
  std::string cdesc;
  std::vector<double> samples;
};

// Unpacks a RAW file and computes per-CFA-channel statistics over the raw Bayer
// mosaic, black-subtracted with the effective black levels. Returns nullopt if
// the file cannot be opened/unpacked, is not an ordinary 2x2 Bayer layout, or
// has no Bayer `raw_image` (X-Trans, Foveon, monochrome/full-color and
// already-demosaiced formats are unsupported this phase). Never throws.
std::optional<RawCfaReport> read_raw_cfa_stats(const std::filesystem::path& raw);

// Unpacks a RAW file and returns the active Bayer mosaic as signed
// black-subtracted samples. Same unsupported-format rules as read_raw_cfa_stats().
std::optional<RawCfaImage> read_raw_cfa_image(const std::filesystem::path& raw);

// Computes a RawCfaReport over a CFA-balanced ROI from an already unpacked
// signed residual mosaic. The requested ROI is clipped and rounded inward by
// cfa_balanced_roi(); the actual ROI is stored in RawCfaReport::measurement_roi.
std::optional<RawCfaReport> raw_cfa_report_for_roi(const RawCfaImage& image,
                                                   const RoiRect& requested);

}  // namespace camera_iq
