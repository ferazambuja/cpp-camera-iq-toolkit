#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace camera_iq {

// Statistics for one CFA mosaic position, computed over signed
// black-subtracted raw residuals sampled at that position.
struct ChannelStats {
  std::string label;              // "R", "G1", "B", "G2" (position channel)
  std::size_t count = 0;          // pixels sampled at this position
  double min = 0.0;               // min signed black-subtracted residual
  double max = 0.0;               // max signed black-subtracted residual
  double mean = 0.0;              // mean signed black-subtracted residual
  double stddev = 0.0;            // population standard deviation of residuals
  double below_black_fraction = 0.0;  // fraction with residual < 0
  double saturated_fraction = 0.0;  // fraction with RAW value >= white
};

// Builds channel labels for the four top-left mosaic positions from LibRaw's
// color descriptor (`cdesc`, e.g. "RGBG") and the COLOR() index at each of the
// positions (0,0)(0,1)(1,0)(1,1). A channel letter that occurs more than once
// (the two greens) is disambiguated with a 1-based ordinal: "G1","G2". Positions
// whose index is out of range are labeled "?".
std::array<std::string, 4> channel_labels(
    const std::string& cdesc, const std::array<int, 4>& color_at_position);

// Computes per-CFA-position statistics over a raw Bayer mosaic.
//
//   data           row-major raw values, length width*height (never null when
//                  width*height > 0)
//   width, height  mosaic dimensions in pixels
//   color_at_position  COLOR() index (0..3) at positions (0,0)(0,1)(1,0)(1,1)
//   cdesc          LibRaw color descriptor, for channel labels
//   black_at_position  black level subtracted per position
//   white          saturation threshold; a RAW value >= white counts saturated
//
// The CFA position of pixel (r,c) is (r % 2, c % 2), i.e. the phase assumes the
// mosaic origin aligns with the active-area (0,0) position. Statistics are over
// signed black-subtracted residuals; saturation is tested on the RAW value
// before subtraction.
std::array<ChannelStats, 4> cfa_plane_stats(
    const std::uint16_t* data, int width, int height,
    const std::array<int, 4>& color_at_position, const std::string& cdesc,
    const std::array<double, 4>& black_at_position, double white);

// Same as cfa_plane_stats(), but each source row advances by row_stride_pixels.
// `data` points at the active-area origin. This lets LibRaw callers crop masked
// frame pixels while still walking the original raw buffer and pitch.
std::array<ChannelStats, 4> cfa_plane_stats_strided(
    const std::uint16_t* data, int width, int height, int row_stride_pixels,
    const std::array<int, 4>& color_at_position, const std::string& cdesc,
    const std::array<double, 4>& black_at_position, double white);

}  // namespace camera_iq
