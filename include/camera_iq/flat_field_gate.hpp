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
inline constexpr double kFlatFieldNearCeilingLevel = 0.98;
inline constexpr double kFlatFieldMaxNearCeilingFraction = 0.01;

// `raw-stats` declares the same signal-referred level independently, and the
// reports present 98% as one cross-command property rather than two that happen
// to agree. Nothing enforced that agreement, so it could drift silently in
// either direction. Deliberately diverging the two is allowed — it just has to
// be a decision: delete this assertion and say so in the reports.
static_assert(kFlatFieldNearCeilingLevel == kRawStatsNearCeilingLevel,
              "flat-field and raw-stats near-ceiling levels are published as "
              "one 98% policy; change both and the reports, or drop this "
              "assertion deliberately");

inline bool valid_flat_field_fraction(double value) {
  return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

// Shared per-position predicate. Both the whole frame and centered gate are
// quality gates; an invalid measurement is never evidence of a clean flat.
inline bool flat_field_near_ceiling_passes(double frame_fraction,
                                           double gate_fraction,
                                           double max_allowed) {
  return valid_flat_field_fraction(frame_fraction) &&
         valid_flat_field_fraction(gate_fraction) &&
         valid_flat_field_fraction(max_allowed) &&
         frame_fraction <= max_allowed && gate_fraction <= max_allowed;
}

struct CfaNearCeilingMeasurement {
  RoiRect frame;
  RoiRect gate;
  std::array<double, 4> fraction_frame{};
  std::array<double, 4> fraction_gate{};
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
