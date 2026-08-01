#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace camera_iq {

// Raw-statistics headroom policy. This is deliberately owned by raw-stats;
// matching numeric thresholds in shading or patch extraction remain separate
// analysis policies with their own serialized provenance.
inline constexpr double kRawStatsNearCeilingLevel = 0.98;

// Signal-referred near-ceiling threshold for one CFA position, or nullopt when
// the metadata cannot define one: a non-finite level/white/black, a level
// outside (0, 1], or a white level at or below black.
//
// Both the full-frame and the ROI statistics paths call this. RAW_STATS.md
// states that they apply the same per-plane `white_level - black[p]` rule; a
// single definition makes that true by construction rather than by two
// implementations agreeing today and drifting later.
std::optional<double> near_ceiling_threshold(
    double white, double black, double level = kRawStatsNearCeilingLevel);

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
  // Fraction with residual >= near_ceiling_level * (white - black). Sensors
  // often plateau below the reported white level — the Fuji X-T100 pins at RAW
  // 16381 against a white_level of 16383 — so `saturated_fraction` can read
  // zero on a completely clipped frame. This is the signal-referred headroom
  // measure that does not depend on hitting `white` exactly.
  double near_ceiling_fraction = 0.0;
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
//   near_ceiling_level  fraction of the signal-referred ceiling
//                  (white - black) at or above which a residual counts as
//                  near-ceiling. Invalid values outside finite (0, 1] produce
//                  an undefined (NaN) near_ceiling_fraction rather than a
//                  plausible-looking zero or one.
//
// The CFA position of pixel (r,c) is (r % 2, c % 2), i.e. the phase assumes the
// mosaic origin aligns with the active-area (0,0) position. Statistics are over
// signed black-subtracted residuals; saturation is tested on the RAW value
// before subtraction.
std::array<ChannelStats, 4> cfa_plane_stats(
    const std::uint16_t* data, int width, int height,
    const std::array<int, 4>& color_at_position, const std::string& cdesc,
    const std::array<double, 4>& black_at_position, double white,
    double near_ceiling_level = kRawStatsNearCeilingLevel);

// Same as cfa_plane_stats(), but each source row advances by row_stride_pixels.
// `data` points at the active-area origin. This lets LibRaw callers crop masked
// frame pixels while still walking the original raw buffer and pitch.
std::array<ChannelStats, 4> cfa_plane_stats_strided(
    const std::uint16_t* data, int width, int height, int row_stride_pixels,
    const std::array<int, 4>& color_at_position, const std::string& cdesc,
    const std::array<double, 4>& black_at_position, double white,
    double near_ceiling_level = kRawStatsNearCeilingLevel);

}  // namespace camera_iq
