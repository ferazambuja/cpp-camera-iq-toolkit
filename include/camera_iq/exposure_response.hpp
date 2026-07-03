#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/manifest.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {

// Per-frame raw-CFA response for one exposure-series member.
// Plane statistics are black-subtracted outputs from read_raw_cfa_stats().
struct ExposureResponseFrame {
  std::string path;
  double shutter_s = 0;
  std::string shutter_str;
  std::array<ChannelStats, 4> planes;
  std::array<double, 4> black_per_channel{0, 0, 0, 0};
  double white_level = 0;
  std::optional<RoiRect> measurement_roi;
};

// One shutter point in a fixed-series response summary. Means/stddevs here are
// averages of per-frame full-frame CFA summaries. The stddev values are spatial
// summaries, not temporal noise estimates.
struct ExposureResponsePoint {
  double shutter_s = 0;
  std::string shutter_str;
  std::vector<ExposureResponseFrame> frames;
  std::array<double, 4> mean_signal_by_plane{0, 0, 0, 0};
  std::array<double, 4> mean_spatial_stddev_by_plane{0, 0, 0, 0};
  bool has_valid_signal_range = false;
  double max_mean_fraction_of_range = 0;
  double max_saturated_fraction = 0;
  // ROI-only readiness fields. Null in JSON when no ROI was measured.
  bool roi_uniformity_checked = false;
  bool roi_uniform = true;
  double max_spatial_stddev_fraction_of_range = 0;
};

// Scientifically conservative readiness summary for the next OECF/PTC work.
// This does not fit an OECF curve and does not compute PTC/noise.
struct ExposureResponseSummary {
  ExposureSeries series;
  std::vector<ExposureResponsePoint> points;
  std::size_t readable_frames = 0;
  std::size_t missing_reports = 0;
  std::size_t usable_oecf_points = 0;
  bool exif_consistent = true;
  bool oecf_candidate = false;
  bool ptc_candidate = false;
  std::vector<std::string> limitations;
};

// Groups black-subtracted raw-CFA reports by shutter for an already detected
// exposure series. `reports_by_path` may be partial; missing reports are counted
// and make the OECF candidate flag false.
ExposureResponseSummary summarize_exposure_response(
    const ExposureSeries& series, const std::vector<ManifestEntry>& entries,
    const std::map<std::string, RawCfaReport>& reports_by_path);

void write_exposure_response_json(
    std::ostream& os, std::string_view root_label,
    const std::vector<ExposureResponseSummary>& summaries);

}  // namespace camera_iq
