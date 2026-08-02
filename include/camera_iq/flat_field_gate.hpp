#pragma once

#include <cmath>
#include <array>
#include <optional>

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/roi.hpp"

namespace camera_iq {

// Shared project policy for deciding whether a RAW flat has enough headroom to
// support either response characterization or correction. These are declared
// analysis choices, not camera-industry standards; every result serializes the
// effective values beside its measured fractions.
inline constexpr double kFlatFieldGateCenterFraction = 0.20;
inline constexpr double kFlatFieldMaxNearCeilingFraction = 0.01;

// The reports present 98% as one signal-referred level shared by `raw-stats`,
// `shading` and the correction screen, so it has one definition rather than
// several that happen to agree. `kRawStatsNearCeilingLevel` in cfa_stats.hpp is
// that definition; this is an alias, not a second policy.
inline constexpr double kFlatFieldNearCeilingLevel = kRawStatsNearCeilingLevel;

// Minimum fraction of a CFA position's samples that must be finite for its
// near-ceiling fraction to mean anything. A fraction is a ratio over finite
// samples only, so one finite low sample reads 0/1 = 0 and looks pristine while
// the rest of the plane is unusable. `shading` already declares 0.90 for map-bin
// coverage; this applies the same declared number to the screening regions,
// which is a new application of it rather than an existing rule.
inline constexpr double kFlatFieldMinFiniteCoverage = 0.90;

inline bool valid_flat_field_fraction(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

// Shared per-position predicate. Both the whole frame and centered gate are
// quality gates; an invalid measurement is never evidence of a clean flat, and
// neither is a valid-looking fraction measured over too few samples.
inline bool flat_field_near_ceiling_passes(double frame_fraction,
                                           double gate_fraction,
                                           double frame_coverage,
                                           double gate_coverage,
                                           double max_allowed,
                                           double min_coverage) {
  return valid_flat_field_fraction(frame_fraction) &&
         valid_flat_field_fraction(gate_fraction) &&
         valid_flat_field_fraction(frame_coverage) &&
         valid_flat_field_fraction(gate_coverage) &&
         valid_flat_field_fraction(max_allowed) &&
         valid_flat_field_fraction(min_coverage) &&
         frame_fraction <= max_allowed && gate_fraction <= max_allowed &&
         frame_coverage >= min_coverage && gate_coverage >= min_coverage;
}

struct CfaNearCeilingMeasurement {
  RoiRect frame;
  RoiRect gate;
  std::array<double, 4> fraction_frame{};
  std::array<double, 4> fraction_gate{};
  // Fraction of each position's samples that were finite. Published beside the
  // near-ceiling fractions because the two together are the evidence: a low
  // near-ceiling fraction over sparse coverage is not a clean flat.
  std::array<double, 4> finite_fraction_frame{};
  std::array<double, 4> finite_fraction_gate{};
};

// Consumer-neutral CFA measurement used by both response characterization and
// correction screening. Non-finite samples are excluded from each plane's
// denominator; a plane with no finite samples reports NaN and therefore fails
// flat_field_near_ceiling_passes().
std::optional<CfaNearCeilingMeasurement> measure_cfa_near_ceiling(
    const double* data, int width, int height, int row_stride_pixels,
    const RoiRect& gate, const std::array<double, 4>& signal_ceiling,
    double near_ceiling_level);

}  // namespace camera_iq
