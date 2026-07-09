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
  double mean_spatial_stddev_dn = 0.0;
  double max_below_black_fraction = 0.0;
  double max_saturated_fraction = 0.0;
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

StepchartRawIsoSummary summarize_stepchart_raw_iso(
    int iso, std::string shutter_token,
    const std::vector<ImatestStepchartZone>& oracle_zones,
    const std::vector<StepchartRawFrameMeasurement>& frames);

}  // namespace camera_iq
