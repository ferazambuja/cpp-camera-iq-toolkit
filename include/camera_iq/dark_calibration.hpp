#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/manifest.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {

struct DarkCalibrationFrame {
  std::string path;
  std::string shutter_str;
  double shutter_s = 0.0;
  std::array<double, 4> metadata_black_by_plane{0, 0, 0, 0};
  std::array<double, 4> mean_residual_by_plane{0, 0, 0, 0};
  std::array<double, 4> measured_dark_raw_by_plane{0, 0, 0, 0};
  std::array<double, 4> spatial_stddev_by_plane{0, 0, 0, 0};
  double max_abs_mean_residual = 0.0;
  bool within_residual_tolerance = false;
};

struct DarkCalibrationSummary {
  std::vector<DarkCalibrationFrame> frames;
  std::size_t candidate_frames = 0;
  std::size_t readable_frames = 0;
  std::size_t frames_within_tolerance = 0;
  std::size_t outlier_frames = 0;
  std::size_t missing_reports = 0;
  double residual_tolerance_dn = 2.0;
  std::array<double, 4> mean_metadata_black_by_plane{0, 0, 0, 0};
  std::array<double, 4> mean_residual_by_plane{0, 0, 0, 0};
  std::array<double, 4> mean_measured_dark_raw_by_plane{0, 0, 0, 0};
  std::array<double, 4> mean_spatial_stddev_by_plane{0, 0, 0, 0};
  std::array<double, 4> mean_in_tolerance_residual_by_plane{0, 0, 0, 0};
  std::array<double, 4> mean_in_tolerance_measured_dark_raw_by_plane{0, 0, 0, 0};
  double max_abs_mean_residual = 0.0;
  bool metadata_black_consistent_with_dark = false;
  std::vector<std::string> limitations;
};

DarkCalibrationSummary summarize_dark_calibration(
    const std::vector<ManifestEntry>& entries,
    const std::map<std::string, RawCfaReport>& reports_by_path,
    double residual_tolerance_dn = 2.0);

void write_dark_calibration_json(std::ostream& os, std::string_view root_label,
                                 const DarkCalibrationSummary& summary);

}  // namespace camera_iq
