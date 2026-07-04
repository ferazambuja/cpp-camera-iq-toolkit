#include "camera_iq/oecf_fit.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

void add_standard_limitations(std::vector<std::string>& limitations) {
  limitations.push_back(
      "not ISO 14524 conformance: this is a relative-exposure linear fit over "
      "existing exposure-response usable points, not a chart-reflectance OECF "
      "or standard-method result.");
  limitations.push_back(
      "PTC/read-noise/dynamic-range not computed: this fit uses per-shutter "
      "mean signal only, not temporal variance or dark-current modeling.");
}

std::string plane_label(const ExposureResponsePoint& point,
                        std::size_t plane_index) {
  if (point.frames.empty()) return "";
  return point.frames.front().planes[plane_index].label;
}

std::optional<OecfPlaneFit> fit_plane(
    const std::vector<const ExposureResponsePoint*>& points,
    std::size_t plane_index, double min_shutter_s) {
  if (points.size() < 3 || min_shutter_s <= 0.0) return std::nullopt;

  OecfPlaneFit fit;
  fit.channel = plane_label(*points.front(), plane_index);
  fit.n_points = points.size();
  fit.points.reserve(points.size());

  double sum_x = 0.0, sum_y = 0.0;
  for (const auto* point : points) {
    OecfFitSample sample;
    sample.shutter_s = point->shutter_s;
    sample.shutter_str = point->shutter_str;
    sample.relative_exposure = point->shutter_s / min_shutter_s;
    sample.signal = point->mean_signal_by_plane[plane_index];
    fit.points.push_back(sample);
    sum_x += sample.relative_exposure;
    sum_y += sample.signal;
  }

  const double n = static_cast<double>(fit.points.size());
  const double mean_x = sum_x / n;
  const double mean_y = sum_y / n;
  double sxx = 0.0, sxy = 0.0, sst = 0.0;
  for (const auto& sample : fit.points) {
    const double dx = sample.relative_exposure - mean_x;
    const double dy = sample.signal - mean_y;
    sxx += dx * dx;
    sxy += dx * dy;
    sst += dy * dy;
  }
  if (sxx <= 0.0) return std::nullopt;

  fit.slope = sxy / sxx;
  fit.intercept = mean_y - fit.slope * mean_x;

  double sse = 0.0;
  double max_abs_residual = 0.0;
  double min_fit = 0.0, max_fit = 0.0;
  bool have_fit_range = false;
  for (auto& sample : fit.points) {
    sample.fitted_signal = fit.intercept + fit.slope * sample.relative_exposure;
    sample.residual = sample.signal - sample.fitted_signal;
    sse += sample.residual * sample.residual;
    max_abs_residual = std::max(max_abs_residual, std::abs(sample.residual));
    if (!have_fit_range) {
      min_fit = max_fit = sample.fitted_signal;
      have_fit_range = true;
    } else {
      min_fit = std::min(min_fit, sample.fitted_signal);
      max_fit = std::max(max_fit, sample.fitted_signal);
    }
  }

  fit.r_squared = sst > 0.0 ? 1.0 - (sse / sst) : (sse == 0.0 ? 1.0 : 0.0);
  const double fit_range = max_fit - min_fit;
  fit.max_nonlinearity_pct =
      fit_range > 0.0 ? (max_abs_residual / fit_range) * 100.0 : 0.0;
  return fit;
}

void write_optional(JsonWriter& w, const std::optional<double>& v) {
  if (v) {
    w.value(*v);
  } else {
    w.null();
  }
}

void write_optional(JsonWriter& w, const std::optional<int>& v) {
  if (v) {
    w.value(*v);
  } else {
    w.null();
  }
}

void write_sample(JsonWriter& w, const OecfFitSample& sample) {
  w.begin_object();
  w.key("shutter_s");
  w.value(sample.shutter_s);
  w.key("shutter_str");
  w.value(sample.shutter_str);
  w.key("relative_exposure");
  w.value(sample.relative_exposure);
  w.key("signal");
  w.value(sample.signal);
  w.key("fitted_signal");
  w.value(sample.fitted_signal);
  w.key("residual");
  w.value(sample.residual);
  w.end_object();
}

void write_plane_fit(JsonWriter& w, const OecfPlaneFit& fit) {
  w.begin_object();
  w.key("channel");
  w.value(fit.channel);
  w.key("n_points");
  w.value(static_cast<std::int64_t>(fit.n_points));
  w.key("slope");
  w.value(fit.slope);
  w.key("intercept");
  w.value(fit.intercept);
  w.key("r_squared");
  w.value(fit.r_squared);
  w.key("max_nonlinearity_pct");
  w.value(fit.max_nonlinearity_pct);
  w.key("points");
  w.begin_array();
  for (const auto& sample : fit.points) write_sample(w, sample);
  w.end_array();
  w.end_object();
}

}  // namespace

OecfSeriesFit fit_oecf_series(const ExposureResponseSummary& summary) {
  OecfSeriesFit out;
  out.series = summary.series;
  out.exif_consistent = summary.exif_consistent;
  out.oecf_candidate = summary.oecf_candidate;
  out.usable_oecf_points = summary.usable_oecf_points;
  add_standard_limitations(out.limitations);

  if (!summary.oecf_candidate) {
    out.limitations.push_back(
        "Fit not emitted: exposure-response summary is not OECF-candidate-ready.");
    return out;
  }

  std::vector<const ExposureResponsePoint*> usable_points;
  usable_points.reserve(summary.points.size());
  double min_shutter_s = 0.0;
  for (const auto& point : summary.points) {
    if (!point.usable_oecf || point.shutter_s <= 0.0) continue;
    usable_points.push_back(&point);
    if (min_shutter_s == 0.0 || point.shutter_s < min_shutter_s) {
      min_shutter_s = point.shutter_s;
    }
  }

  if (usable_points.size() < 3) {
    out.limitations.push_back(
        "Fit not emitted: fewer than three usable OECF points remain after "
        "black, saturation, and ROI-uniformity gates.");
    return out;
  }

  out.fit_candidate = true;
  for (std::size_t i = 0; i < out.plane_fits.size(); ++i) {
    out.plane_fits[i] = fit_plane(usable_points, i, min_shutter_s);
    if (!out.plane_fits[i]) out.fit_candidate = false;
  }
  return out;
}

void write_oecf_fit_json(std::ostream& os, std::string_view root_label,
                         const std::vector<OecfSeriesFit>& fits) {
  JsonWriter w(os);
  w.begin_object();
  w.key("tool");
  w.value("camera_iq");
  w.key("mode");
  w.value("oecf-fit");
  w.key("root");
  w.value(root_label);
  w.key("series_count");
  w.value(static_cast<std::int64_t>(fits.size()));
  w.key("series");
  w.begin_array();
  for (const auto& fit : fits) {
    w.begin_object();
    w.key("directory");
    w.value(fit.series.directory);
    w.key("group");
    w.value(fit.series.group);
    w.key("aperture");
    write_optional(w, fit.series.aperture);
    w.key("iso");
    write_optional(w, fit.series.iso);
    w.key("oecf_candidate");
    w.value(fit.oecf_candidate);
    w.key("exif_consistent");
    w.value(fit.exif_consistent);
    w.key("fit_candidate");
    w.value(fit.fit_candidate);
    w.key("usable_oecf_points");
    w.value(static_cast<std::int64_t>(fit.usable_oecf_points));
    w.key("limitations");
    w.begin_array();
    for (const auto& limitation : fit.limitations) w.value(limitation);
    w.end_array();
    w.key("plane_fits");
    w.begin_array();
    for (const auto& plane : fit.plane_fits) {
      if (plane) {
        write_plane_fit(w, *plane);
      } else {
        w.null();
      }
    }
    w.end_array();
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

}  // namespace camera_iq
