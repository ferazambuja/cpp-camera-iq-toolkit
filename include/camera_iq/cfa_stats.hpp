#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace camera_iq {

// Statistics for one CFA mosaic position, computed over the black-subtracted
// raw values sampled at that position.
struct ChannelStats {
  std::string label;              // "R", "G1", "B", "G2" (position channel)
  std::size_t count = 0;          // pixels sampled at this position
  double min = 0.0;               // min black-subtracted value (>= 0)
  double max = 0.0;               // max black-subtracted value
  double mean = 0.0;              // mean black-subtracted value
  double stddev = 0.0;            // population standard deviation
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
//   black_at_position  black level subtracted per position (clamped at >= 0)
//   white          saturation threshold; a RAW value >= white counts saturated
//
// The CFA position of pixel (r,c) is (r % 2, c % 2), i.e. the phase assumes the
// mosaic origin aligns with the (0,0) position — exact for zero-margin sensors
// (see effective_black_levels()'s margin caveat). Statistics are over the
// black-subtracted value (clamped at 0); saturation is tested on the RAW value
// before subtraction.
std::array<ChannelStats, 4> cfa_plane_stats(
    const std::uint16_t* data, int width, int height,
    const std::array<int, 4>& color_at_position, const std::string& cdesc,
    const std::array<double, 4>& black_at_position, double white);

}  // namespace camera_iq
