#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/imatest_stepchart.hpp"
#include "camera_iq/roi.hpp"

namespace camera_iq {

struct StepchartRawZoneMeasurement {
  int zone = 0;
  std::optional<RoiRect> measurement_roi;
  std::array<ChannelStats, 4> planes;
  std::array<std::vector<double>, 4> plane_samples;
};

struct StepchartRawFrameMeasurement {
  std::string file_label;
  std::vector<StepchartRawZoneMeasurement> zones;
};

struct StepchartRawPlaneSummary {
  std::string channel;
  std::size_t frame_count = 0;
  double mean_dn = 0.0;
  double temporal_stddev_of_zone_mean_dn = 0.0;
  std::size_t aligned_pixel_count = 0;
  double temporal_variance_per_pixel_dn2 = 0.0;
  double temporal_stddev_per_pixel_dn = 0.0;
  double mean_spatial_stddev_dn = 0.0;
  double max_below_black_fraction = 0.0;
  double max_saturated_fraction = 0.0;
  bool ptc_fit_included = false;
  std::string ptc_fit_exclusion_reason = "not_evaluated";
};

struct StepchartRawZoneSummary {
  int zone = 0;
  double log_exposure = 0.0;
  std::size_t frame_count = 0;
  std::optional<RoiRect> measurement_roi;
  std::array<StepchartRawPlaneSummary, 4> planes;
};

struct StepchartRawIsoSummary {
  int iso = 0;
  std::string shutter_token;
  std::size_t frame_count = 0;
  bool ptc_candidate = false;
  bool dynamic_range_candidate = false;
  std::vector<std::string> limitations;
  std::vector<StepchartRawZoneSummary> zones;
};

struct StepchartRawOracleGateDiagnostics {
  bool green_monotone_nonincreasing = false;
  double green_correlation = 0.0;
  double min_green_correlation = 0.98;
  double linear_gain = 0.0;
  double additive_offset = 0.0;
  bool passes = false;
};

struct StepchartRawPtcPlaneFit {
  std::string channel;
  std::size_t fitted_zone_count = 0;
  double min_log_exposure = -1.6;
  double slope_variance_dn2_per_mean_dn = 0.0;
  double intercept_variance_dn2 = 0.0;
  double r_squared = 0.0;
  bool fit_valid = false;
  std::string reason = "not_evaluated";
};

struct StepchartRawPtcDiagnostic {
  std::string method =
      "dn_referred_per_pixel_temporal_variance_vs_mean";
  bool dn_referred_ptc_candidate = false;
  bool electron_calibrated_gain_candidate = false;
  bool dynamic_range_candidate = false;
  std::array<StepchartRawPtcPlaneFit, 4> planes;
  std::vector<std::string> limitations;
};

StepchartRawIsoSummary summarize_stepchart_raw_iso(
    int iso, std::string shutter_token,
    const std::vector<ImatestStepchartZone>& oracle_zones,
    const std::vector<StepchartRawFrameMeasurement>& frames);

// Empirical oracle-ladder gate. The oracle already pins each zone's relative
// exposure, so a correct extraction is checkable: green zone means must be
// non-increasing in zone order (deep-shadow ties allowed) and strongly
// linearly correlated with 10^log_exposure. A corner seed that lands off the
// physical zones — wrong position or a chart-layout model mismatch (e.g. a
// strip model on a ring-layout ISO 14524 chart) — otherwise produces
// confident garbage.
//
// evaluate_ returns the gate diagnostics (monotone flag, correlation, linear
// fit, pass flag) without deciding; it throws only for malformed summaries
// (no green plane, fewer than 3 zones).
StepchartRawOracleGateDiagnostics evaluate_stepchart_raw_iso_against_oracle(
    const StepchartRawIsoSummary& summary);

// Throwing form: refuses with a std::runtime_error naming the violated
// check. Its verdict is evaluate_'s pass flag, so the serialized diagnostics
// and the accept/reject decision cannot drift apart.
void validate_stepchart_raw_iso_against_oracle(
    const StepchartRawIsoSummary& summary);

StepchartRawPtcDiagnostic analyze_stepchart_raw_ptc_diagnostic(
    StepchartRawIsoSummary& summary);

}  // namespace camera_iq
