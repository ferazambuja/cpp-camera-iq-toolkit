#include "camera_iq/stepchart_raw.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace camera_iq {
namespace {

bool same_roi(const RoiRect& a, const RoiRect& b) {
  return a.x == b.x && a.y == b.y && a.width == b.width &&
         a.height == b.height;
}

double population_stddev(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  double mean = 0.0;
  for (double v : values) mean += v;
  mean /= static_cast<double>(values.size());
  double ss = 0.0;
  for (double v : values) {
    const double d = v - mean;
    ss += d * d;
  }
  return std::sqrt(ss / static_cast<double>(values.size()));
}

double mean_value(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  double sum = 0.0;
  for (double v : values) sum += v;
  return sum / static_cast<double>(values.size());
}

const StepchartRawZoneMeasurement& find_zone(
    const StepchartRawFrameMeasurement& frame, int zone) {
  const StepchartRawZoneMeasurement* found = nullptr;
  for (const auto& measurement : frame.zones) {
    if (measurement.zone != zone) continue;
    if (found != nullptr) {
      throw std::runtime_error("Stepchart raw: duplicate zone " +
                               std::to_string(zone) + " in " +
                               frame.file_label);
    }
    found = &measurement;
  }
  if (found == nullptr) {
    throw std::runtime_error("Stepchart raw: missing zone " +
                             std::to_string(zone) + " in " +
                             frame.file_label);
  }
  return *found;
}

}  // namespace

StepchartRawIsoSummary summarize_stepchart_raw_iso(
    int iso, std::string shutter_token,
    const std::vector<ImatestStepchartZone>& oracle_zones,
    const std::vector<StepchartRawFrameMeasurement>& frames) {
  if (oracle_zones.empty()) {
    throw std::runtime_error("Stepchart raw: no oracle zones");
  }
  if (frames.empty()) {
    throw std::runtime_error("Stepchart raw: no raw frames");
  }

  StepchartRawIsoSummary out;
  out.iso = iso;
  out.shutter_token = std::move(shutter_token);
  out.frame_count = frames.size();
  out.ptc_candidate = false;
  out.dynamic_range_candidate = false;
  out.limitations = {
      "DN-only raw zone summaries: no electron calibration, ISO 14524 "
      "conformance, engineering dynamic range, or PRNU is claimed.",
      "temporal_stddev_of_zone_mean_dn is the repeat-frame spread of ROI means, "
      "not per-pixel photon-transfer variance."};
  out.zones.reserve(oracle_zones.size());

  for (const auto& oracle_zone : oracle_zones) {
    StepchartRawZoneSummary zone;
    zone.zone = oracle_zone.zone;
    zone.log_exposure = oracle_zone.log_exposure;
    zone.frame_count = frames.size();

    std::optional<RoiRect> common_roi;
    bool common_roi_valid = true;
    std::array<std::vector<double>, 4> means;
    std::array<std::vector<double>, 4> spatial_stddevs;
    std::array<double, 4> max_below{0, 0, 0, 0};
    std::array<double, 4> max_sat{0, 0, 0, 0};
    std::array<std::string, 4> labels;

    for (const auto& frame : frames) {
      const auto& measurement = find_zone(frame, oracle_zone.zone);
      if (measurement.measurement_roi) {
        if (!common_roi) {
          common_roi = measurement.measurement_roi;
        } else if (!same_roi(*common_roi, *measurement.measurement_roi)) {
          common_roi_valid = false;
        }
      } else {
        common_roi_valid = false;
      }
      for (std::size_t p = 0; p < 4; ++p) {
        const auto& plane = measurement.planes[p];
        if (labels[p].empty()) labels[p] = plane.label;
        means[p].push_back(plane.mean);
        spatial_stddevs[p].push_back(plane.stddev);
        max_below[p] = std::max(max_below[p], plane.below_black_fraction);
        max_sat[p] = std::max(max_sat[p], plane.saturated_fraction);
      }
    }
    if (common_roi_valid) {
      zone.measurement_roi = common_roi;
    }

    for (std::size_t p = 0; p < 4; ++p) {
      auto& plane = zone.planes[p];
      plane.channel = labels[p];
      plane.frame_count = means[p].size();
      plane.mean_dn = mean_value(means[p]);
      plane.temporal_stddev_of_zone_mean_dn = population_stddev(means[p]);
      plane.mean_spatial_stddev_dn = mean_value(spatial_stddevs[p]);
      plane.max_below_black_fraction = max_below[p];
      plane.max_saturated_fraction = max_sat[p];
    }
    out.zones.push_back(std::move(zone));
  }

  return out;
}

}  // namespace camera_iq
