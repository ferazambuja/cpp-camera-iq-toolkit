#include "camera_iq/noise.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <utility>

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

struct Stats {
  std::size_t n = 0;
  double mean = 0.0;
  double stddev = 0.0;
};

Stats mean_stddev(const std::vector<double>& values) {
  Stats out;
  out.n = values.size();
  if (values.empty()) return out;
  double sum = 0.0;
  for (double v : values) sum += v;
  out.mean = sum / static_cast<double>(values.size());
  double ss = 0.0;
  for (double v : values) {
    const double d = v - out.mean;
    ss += d * d;
  }
  out.stddev = std::sqrt(ss / static_cast<double>(values.size()));
  return out;
}

double median(std::vector<double> values) {
  if (values.empty()) return 0.0;
  const std::size_t mid = values.size() / 2;
  std::nth_element(values.begin(), values.begin() + static_cast<long>(mid),
                   values.end());
  double med = values[mid];
  if ((values.size() & 1U) == 0U) {
    std::nth_element(values.begin(), values.begin() + static_cast<long>(mid - 1),
                     values.end());
    med = (med + values[mid - 1]) / 2.0;
  }
  return med;
}

double mad_stddev(std::vector<double> values) {
  if (values.empty()) return 0.0;
  const double med = median(values);
  for (double& v : values) v = std::abs(v - med);
  return median(std::move(values)) * 1.4826;
}

void write_optional(JsonWriter& w, const std::optional<double>& value) {
  if (value) {
    w.value(*value);
  } else {
    w.null();
  }
}

void write_optional(JsonWriter& w, const std::optional<int>& value) {
  if (value) {
    w.value(*value);
  } else {
    w.null();
  }
}

void write_optional(JsonWriter& w, const std::optional<RoiRect>& roi) {
  if (!roi) {
    w.null();
    return;
  }
  w.begin_object();
  w.key("x");
  w.value(roi->x);
  w.key("y");
  w.value(roi->y);
  w.key("width");
  w.value(roi->width);
  w.key("height");
  w.value(roi->height);
  w.end_object();
}

void write_plane(JsonWriter& w, const NoisePlaneEstimate& plane) {
  w.begin_object();
  w.key("channel");
  w.value(plane.channel);
  w.key("unit");
  w.value("DN");
  w.key("sample_count");
  w.value(static_cast<std::int64_t>(plane.sample_count));
  w.key("temporal_noise_dn");
  w.value(plane.temporal_noise_dn);
  w.key("difference_mean_dn");
  w.value(plane.difference_mean_dn);
  w.key("difference_stddev_dn");
  w.value(plane.difference_stddev_dn);
  w.key("pair_mean_spatial_stddev_dn");
  w.value(plane.pair_mean_spatial_stddev_dn);
  w.key("pair_mean_mad_stddev_dn");
  w.value(plane.pair_mean_mad_stddev_dn);
  w.key("dsnu_variance_dn2");
  w.value(plane.dsnu_variance_dn2);
  w.key("dsnu_moment_dn");
  write_optional(w, plane.dsnu_moment_dn);
  w.key("dsnu_moment_reason");
  w.value(plane.dsnu_moment_reason);
  w.key("dsnu_robust_variance_dn2");
  w.value(plane.dsnu_robust_variance_dn2);
  w.key("dsnu_robust_mad_dn");
  write_optional(w, plane.dsnu_robust_mad_dn);
  w.key("dsnu_robust_reason");
  w.value(plane.dsnu_robust_reason);
  w.end_object();
}

void write_pair(JsonWriter& w, const NoisePairEstimate& pair) {
  w.begin_object();
  w.key("first_path");
  w.value(pair.first_path);
  w.key("second_path");
  w.value(pair.second_path);
  w.key("shutter_str");
  w.value(pair.shutter_str);
  w.key("shutter_s");
  w.value(pair.shutter_s);
  w.key("iso");
  write_optional(w, pair.iso);
  w.key("aperture");
  write_optional(w, pair.aperture);
  w.key("measurement_roi");
  write_optional(w, pair.measurement_roi);
  w.key("limitation");
  w.value(pair.limitation);
  w.key("planes");
  w.begin_array();
  for (const auto& plane : pair.planes) write_plane(w, plane);
  w.end_array();
  w.end_object();
}

}  // namespace

std::optional<std::string> validate_noise_pair_compatibility(
    const RawCfaImage& first, const RawCfaImage& second) {
  if (first.width != second.width || first.height != second.height ||
      first.row_stride_pixels != second.row_stride_pixels) {
    return "dimension_or_stride_mismatch";
  }
  if (first.width <= 0 || first.height <= 0 ||
      first.row_stride_pixels < first.width) {
    return "dimension_or_stride_mismatch";
  }
  if (first.color_at_position != second.color_at_position ||
      first.cdesc != second.cdesc) {
    return "cfa_phase_mismatch";
  }
  const auto required_samples =
      static_cast<std::size_t>(first.row_stride_pixels) *
      static_cast<std::size_t>(first.height);
  if (first.samples.size() < required_samples ||
      second.samples.size() < required_samples ||
      first.samples.size() != second.samples.size()) {
    return "sample_count_mismatch";
  }
  return std::nullopt;
}

NoisePairEstimate compute_noise_pair_estimate(
    const RawCfaImage& first, const RawCfaImage& second,
    std::string_view first_path, std::string_view second_path,
    std::string_view shutter_str, double shutter_s, std::optional<int> iso,
    std::optional<double> aperture, std::optional<RoiRect> requested_roi) {
  NoisePairEstimate out;
  out.first_path = std::string(first_path);
  out.second_path = std::string(second_path);
  out.shutter_str = std::string(shutter_str);
  out.shutter_s = shutter_s;
  out.iso = iso;
  out.aperture = aperture;
  out.measurement_roi = requested_roi;
  out.limitation =
      "single matched dark pair: temporal noise is DN-only and approximates "
      "read noise only when dark current is negligible.";

  if (const auto err = validate_noise_pair_compatibility(first, second)) {
    out.limitation = *err;
    return out;
  }

  const auto labels = channel_labels(first.cdesc, first.color_at_position);
  const int x0 = requested_roi ? requested_roi->x : 0;
  const int y0 = requested_roi ? requested_roi->y : 0;
  const int width = requested_roi ? requested_roi->width : first.width;
  const int height = requested_roi ? requested_roi->height : first.height;
  const int x1 = std::min(first.width, x0 + width);
  const int y1 = std::min(first.height, y0 + height);

  std::array<std::vector<double>, 4> diffs;
  std::array<std::vector<double>, 4> means;
  for (int r = std::max(0, y0); r < y1; ++r) {
    for (int c = std::max(0, x0); c < x1; ++c) {
      const std::size_t p = static_cast<std::size_t>((r & 1) * 2 + (c & 1));
      const std::size_t idx = static_cast<std::size_t>(r) *
                                  static_cast<std::size_t>(first.row_stride_pixels) +
                              static_cast<std::size_t>(c);
      const double a = first.samples[idx];
      const double b = second.samples[idx];
      diffs[p].push_back(a - b);
      means[p].push_back((a + b) / 2.0);
    }
  }

  for (std::size_t p = 0; p < out.planes.size(); ++p) {
    NoisePlaneEstimate& plane = out.planes[p];
    plane.channel = labels[p];
    const Stats diff_stats = mean_stddev(diffs[p]);
    const Stats mean_stats = mean_stddev(means[p]);
    plane.sample_count = diff_stats.n;
    plane.difference_mean_dn = diff_stats.mean;
    plane.difference_stddev_dn = diff_stats.stddev;
    plane.temporal_noise_dn = diff_stats.stddev / std::sqrt(2.0);
    plane.pair_mean_spatial_stddev_dn = mean_stats.stddev;
    plane.pair_mean_mad_stddev_dn = mad_stddev(means[p]);
    plane.dsnu_variance_dn2 =
        mean_stats.stddev * mean_stats.stddev -
        (plane.temporal_noise_dn * plane.temporal_noise_dn) / 2.0;
    if (plane.dsnu_variance_dn2 >= 0.0) {
      plane.dsnu_moment_dn = std::sqrt(plane.dsnu_variance_dn2);
      plane.dsnu_moment_reason = "ok";
    } else {
      plane.dsnu_moment_dn = std::nullopt;
      plane.dsnu_moment_reason = "dsnu_below_temporal_floor";
    }
    // Robust DSNU mirrors the moment estimate: subtract the same temporal
    // floor (sigma_temporal^2 / 2 for an N=2 pair mean) from a hot-pixel-robust
    // MAD spread, so the two DSNU columns are on the same scale.  A MAD spread
    // dominated by leftover temporal noise (bulk fixed-pattern below the floor)
    // clamps to null; if the moment DSNU is still large, the spread is
    // tail-sensitive rather than supported by the robust bulk estimate.
    plane.dsnu_robust_variance_dn2 =
        plane.pair_mean_mad_stddev_dn * plane.pair_mean_mad_stddev_dn -
        (plane.temporal_noise_dn * plane.temporal_noise_dn) / 2.0;
    if (plane.dsnu_robust_variance_dn2 >= 0.0) {
      plane.dsnu_robust_mad_dn = std::sqrt(plane.dsnu_robust_variance_dn2);
      plane.dsnu_robust_reason = "ok";
    } else {
      plane.dsnu_robust_mad_dn = std::nullopt;
      plane.dsnu_robust_reason = "dsnu_below_temporal_floor";
    }
  }
  return out;
}

std::array<NoiseDarkCurrentPlaneFit, 4> fit_dark_current_diagnostic(
    const std::vector<std::pair<double, std::array<double, 4>>>& points,
    const std::array<std::string, 4>& labels) {
  std::array<NoiseDarkCurrentPlaneFit, 4> out;
  for (std::size_t p = 0; p < out.size(); ++p) {
    auto& fit = out[p];
    fit.channel = labels[p];
    fit.n_points = points.size();
    if (points.size() < 3) {
      fit.reason = "fewer_than_three_dark_current_points";
      continue;
    }
    double sum_x = 0.0, sum_y = 0.0;
    for (const auto& point : points) {
      sum_x += point.first;
      sum_y += point.second[p];
    }
    const double n = static_cast<double>(points.size());
    const double mean_x = sum_x / n;
    const double mean_y = sum_y / n;
    double sxx = 0.0, sxy = 0.0, sst = 0.0;
    for (const auto& point : points) {
      const double dx = point.first - mean_x;
      const double dy = point.second[p] - mean_y;
      sxx += dx * dx;
      sxy += dx * dy;
      sst += dy * dy;
    }
    if (sxx <= 0.0) {
      fit.reason = "dark_current_shutter_range_missing";
      continue;
    }
    fit.slope_dn_per_s = sxy / sxx;
    fit.intercept_dn = mean_y - fit.slope_dn_per_s * mean_x;
    double sse = 0.0;
    for (const auto& point : points) {
      const double predicted = fit.intercept_dn + fit.slope_dn_per_s * point.first;
      const double residual = point.second[p] - predicted;
      sse += residual * residual;
    }
    fit.r_squared = sst > 0.0 ? 1.0 - sse / sst : (sse == 0.0 ? 1.0 : 0.0);
    fit.measurable = fit.r_squared >= 0.5 && std::abs(fit.slope_dn_per_s) > 1.0;
    fit.reason = fit.measurable ? "ok" : "dark_current_not_measurable_from_ladder";
  }
  return out;
}

void write_noise_json(std::ostream& os, const NoiseSummary& summary) {
  JsonWriter w(os);
  w.begin_object();
  w.key("tool");
  w.value("camera_iq");
  w.key("mode");
  w.value("noise");
  w.key("root");
  w.value(summary.root_label);
  w.key("candidate_frames");
  w.value(static_cast<std::int64_t>(summary.candidate_frames));
  w.key("readable_frames");
  w.value(static_cast<std::int64_t>(summary.readable_frames));
  w.key("in_tolerance_frames");
  w.value(static_cast<std::int64_t>(summary.in_tolerance_frames));
  w.key("excluded_frames");
  w.value(static_cast<std::int64_t>(summary.excluded_frames));
  w.key("matched_pair_count");
  w.value(static_cast<std::int64_t>(summary.matched_pair_count));
  w.key("single_pair_only");
  w.value(summary.single_pair_only);
  w.key("gain_candidate");
  w.value(summary.gain_candidate);
  w.key("gain_not_supported_reason");
  w.value(summary.gain_not_supported_reason);
  w.key("ptc_candidate");
  w.value(summary.ptc_candidate);
  w.key("ptc_not_supported_reason");
  w.value(summary.ptc_not_supported_reason);
  w.key("dr_candidate");
  w.value(summary.dr_candidate);
  w.key("dr_not_supported_reason");
  w.value(summary.dr_not_supported_reason);
  w.key("limitations");
  w.begin_array();
  for (const auto& limitation : summary.limitations) w.value(limitation);
  w.end_array();
  w.key("pairs");
  w.begin_array();
  for (const auto& pair : summary.pairs) write_pair(w, pair);
  w.end_array();
  w.key("dark_current_fits");
  w.begin_array();
  for (const auto& fit : summary.dark_current_fits) {
    w.begin_object();
    w.key("channel");
    w.value(fit.channel);
    w.key("n_points");
    w.value(static_cast<std::int64_t>(fit.n_points));
    w.key("slope_dn_per_s");
    w.value(fit.slope_dn_per_s);
    w.key("intercept_dn");
    w.value(fit.intercept_dn);
    w.key("r_squared");
    w.value(fit.r_squared);
    w.key("measurable");
    w.value(fit.measurable);
    w.key("reason");
    w.value(fit.reason);
    w.end_object();
  }
  w.end_array();
  w.key("exclusions");
  w.begin_array();
  for (const auto& exclusion : summary.exclusions) {
    w.begin_object();
    w.key("path");
    w.value(exclusion.path);
    w.key("reason");
    w.value(exclusion.reason);
    w.key("max_abs_mean_residual");
    w.value(exclusion.max_abs_mean_residual);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

}  // namespace camera_iq
