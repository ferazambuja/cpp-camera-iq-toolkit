#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/raw_meta.hpp"
#include "camera_iq/roi.hpp"

namespace camera_iq {

struct NoisePlaneEstimate {
  std::string channel;
  std::size_t sample_count = 0;
  double temporal_noise_dn = 0.0;
  double difference_mean_dn = 0.0;
  double difference_stddev_dn = 0.0;
  double pair_mean_spatial_stddev_dn = 0.0;
  double pair_mean_mad_stddev_dn = 0.0;
  double dsnu_variance_dn2 = 0.0;
  std::optional<double> dsnu_moment_dn;
  std::string dsnu_moment_reason;
  double dsnu_robust_mad_dn = 0.0;
};

struct NoisePairEstimate {
  std::string first_path;
  std::string second_path;
  std::string shutter_str;
  double shutter_s = 0.0;
  std::optional<int> iso;
  std::optional<double> aperture;
  std::optional<RoiRect> measurement_roi;
  std::array<NoisePlaneEstimate, 4> planes;
  std::string limitation;
};

struct NoiseExcludedFrame {
  std::string path;
  std::string reason;
  double max_abs_mean_residual = 0.0;
};

struct NoiseDarkCurrentPlaneFit {
  std::string channel;
  std::size_t n_points = 0;
  double slope_dn_per_s = 0.0;
  double intercept_dn = 0.0;
  double r_squared = 0.0;
  bool measurable = false;
  std::string reason;
};

struct NoiseSummary {
  std::string root_label;
  std::size_t candidate_frames = 0;
  std::size_t readable_frames = 0;
  std::size_t in_tolerance_frames = 0;
  std::size_t excluded_frames = 0;
  std::size_t matched_pair_count = 0;
  bool single_pair_only = false;
  bool gain_candidate = false;
  bool ptc_candidate = false;
  bool dr_candidate = false;
  std::string gain_not_supported_reason;
  std::string ptc_not_supported_reason;
  std::string dr_not_supported_reason;
  std::vector<NoisePairEstimate> pairs;
  std::array<NoiseDarkCurrentPlaneFit, 4> dark_current_fits;
  std::vector<NoiseExcludedFrame> exclusions;
  std::vector<std::string> limitations;
};

std::optional<std::string> validate_noise_pair_compatibility(
    const RawCfaImage& first, const RawCfaImage& second);

NoisePairEstimate compute_noise_pair_estimate(
    const RawCfaImage& first, const RawCfaImage& second,
    std::string_view first_path, std::string_view second_path,
    std::string_view shutter_str, double shutter_s, std::optional<int> iso,
    std::optional<double> aperture, std::optional<RoiRect> requested_roi = {});

std::array<NoiseDarkCurrentPlaneFit, 4> fit_dark_current_diagnostic(
    const std::vector<std::pair<double, std::array<double, 4>>>& points,
    const std::array<std::string, 4>& labels);

void write_noise_json(std::ostream& os, const NoiseSummary& summary);

}  // namespace camera_iq
