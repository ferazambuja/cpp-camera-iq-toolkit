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

struct OracleGateVectors {
  std::vector<double> green;
  std::vector<double> rel_exposure;
};

struct LinearFit {
  std::size_t n = 0;
  double slope = 0.0;
  double intercept = 0.0;
  double r_squared = 0.0;
  bool valid = false;
};

struct PixelVarianceStats {
  std::size_t aligned_pixel_count = 0;
  double temporal_variance_per_pixel = 0.0;
};

OracleGateVectors oracle_gate_vectors(const StepchartRawIsoSummary& summary) {
  OracleGateVectors out;
  out.green.reserve(summary.zones.size());
  out.rel_exposure.reserve(summary.zones.size());
  for (const auto& zone : summary.zones) {
    double sum = 0.0;
    int count = 0;
    for (const auto& plane : zone.planes) {
      if (!plane.channel.empty() &&
          (plane.channel[0] == 'G' || plane.channel[0] == 'g')) {
        sum += plane.mean_dn;
        ++count;
      }
    }
    if (count == 0) {
      throw std::runtime_error(
          "Stepchart raw gate: no green plane in zone summaries");
    }
    out.green.push_back(sum / count);
    out.rel_exposure.push_back(std::pow(10.0, zone.log_exposure));
  }
  if (out.green.size() < 3) {
    throw std::runtime_error(
        "Stepchart raw gate: need at least 3 zones to validate");
  }
  return out;
}

bool green_is_monotone_nonincreasing(const std::vector<double>& green) {
  for (std::size_t i = 0; i + 1 < green.size(); ++i) {
    const double tolerance = std::max(5.0, 0.02 * green[i]);
    if (green[i + 1] > green[i] + tolerance) return false;
  }
  return true;
}

PixelVarianceStats temporal_pixel_variance(
    const std::vector<std::vector<double>>& samples_by_frame) {
  PixelVarianceStats out;
  if (samples_by_frame.empty()) return out;
  const std::size_t pixel_count = samples_by_frame.front().size();
  if (pixel_count == 0) return out;
  if (samples_by_frame.size() < 2) return out;
  for (const auto& samples : samples_by_frame) {
    if (samples.size() != pixel_count) {
      throw std::runtime_error(
          "Stepchart raw: aligned temporal pixel sample count mismatch");
    }
  }

  double variance_sum = 0.0;
  for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
    double mean = 0.0;
    for (const auto& samples : samples_by_frame) mean += samples[pixel];
    mean /= static_cast<double>(samples_by_frame.size());
    double ss = 0.0;
    for (const auto& samples : samples_by_frame) {
      const double d = samples[pixel] - mean;
      ss += d * d;
    }
    variance_sum += ss / static_cast<double>(samples_by_frame.size() - 1);
  }
  out.aligned_pixel_count = pixel_count;
  out.temporal_variance_per_pixel =
      variance_sum / static_cast<double>(pixel_count);
  return out;
}

LinearFit fit_line(const std::vector<double>& x, const std::vector<double>& y) {
  LinearFit out;
  if (x.size() != y.size() || x.size() < 2) return out;
  out.n = x.size();
  double mean_x = 0.0;
  double mean_y = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    mean_x += x[i];
    mean_y += y[i];
  }
  mean_x /= static_cast<double>(x.size());
  mean_y /= static_cast<double>(y.size());
  double sxx = 0.0;
  double sxy = 0.0;
  double syy = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double dx = x[i] - mean_x;
    const double dy = y[i] - mean_y;
    sxx += dx * dx;
    sxy += dx * dy;
    syy += dy * dy;
  }
  if (sxx <= 0.0 || syy <= 0.0) return out;
  out.slope = sxy / sxx;
  out.intercept = mean_y - out.slope * mean_x;
  double sse = 0.0;
  for (std::size_t i = 0; i < x.size(); ++i) {
    const double residual = y[i] - (out.slope * x[i] + out.intercept);
    sse += residual * residual;
  }
  out.r_squared = 1.0 - sse / syy;
  out.valid = std::isfinite(out.slope) && std::isfinite(out.intercept) &&
              std::isfinite(out.r_squared);
  return out;
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
    std::array<std::vector<std::vector<double>>, 4> samples_by_frame;
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
        if (!measurement.plane_samples[p].empty()) {
          samples_by_frame[p].push_back(measurement.plane_samples[p]);
        }
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
      if (!samples_by_frame[p].empty()) {
        if (!common_roi_valid) {
          throw std::runtime_error(
              "Stepchart raw: aligned temporal pixel samples require identical "
              "actual ROI across frames");
        }
        if (samples_by_frame[p].size() != frames.size()) {
          throw std::runtime_error(
              "Stepchart raw: aligned temporal pixel samples missing in one or "
              "more frames");
        }
        const auto pixel_stats = temporal_pixel_variance(samples_by_frame[p]);
        plane.aligned_pixel_count = pixel_stats.aligned_pixel_count;
        plane.temporal_variance_per_pixel_dn2 =
            pixel_stats.temporal_variance_per_pixel;
        plane.temporal_stddev_per_pixel_dn =
            std::sqrt(plane.temporal_variance_per_pixel_dn2);
      }
      plane.mean_spatial_stddev_dn = mean_value(spatial_stddevs[p]);
      plane.max_below_black_fraction = max_below[p];
      plane.max_saturated_fraction = max_sat[p];
    }
    out.zones.push_back(std::move(zone));
  }

  return out;
}

StepchartRawPtcDiagnostic analyze_stepchart_raw_ptc_diagnostic(
    StepchartRawIsoSummary& summary) {
  constexpr double kMinLogExposure = -1.6;
  constexpr std::size_t kMinFitZones = 5;

  StepchartRawPtcDiagnostic out;
  out.dn_referred_ptc_candidate = true;
  out.electron_calibrated_gain_candidate = false;
  out.dynamic_range_candidate = false;
  out.limitations = {
      "PTC fit is DN-referred: slope/intercept are in raw DN units, not "
      "electron-calibrated gain or read noise.",
      "Fit uses per-pixel temporal variance over aligned repeat frames; "
      "low-signal flare/noise-floor tail zones are excluded.",
      "No full-well, ISO speed, PRNU, or engineering dynamic range is claimed."};

  for (std::size_t p = 0; p < out.planes.size(); ++p) {
    StepchartRawPtcPlaneFit& fit = out.planes[p];
    fit.min_log_exposure = kMinLogExposure;
    if (!summary.zones.empty()) {
      fit.channel = summary.zones.front().planes[p].channel;
    }

    std::vector<double> means;
    std::vector<double> variances;
    for (auto& zone : summary.zones) {
      auto& plane = zone.planes[p];
      plane.ptc_fit_included = false;
      plane.ptc_fit_exclusion_reason = "not_evaluated";
      if (fit.channel.empty()) fit.channel = plane.channel;

      if (plane.aligned_pixel_count == 0 ||
          !(plane.temporal_variance_per_pixel_dn2 > 0.0)) {
        plane.ptc_fit_exclusion_reason = "no_aligned_pixel_temporal_variance";
      } else if (plane.max_saturated_fraction > 0.0) {
        plane.ptc_fit_exclusion_reason = "saturated";
      } else if (plane.max_below_black_fraction > 0.0) {
        plane.ptc_fit_exclusion_reason = "below_black";
      } else if (zone.log_exposure < kMinLogExposure) {
        plane.ptc_fit_exclusion_reason = "low_signal_flare_noise_floor_tail";
      } else if (!(plane.mean_dn > 0.0)) {
        plane.ptc_fit_exclusion_reason = "nonpositive_mean";
      } else {
        plane.ptc_fit_included = true;
        plane.ptc_fit_exclusion_reason = "included";
        means.push_back(plane.mean_dn);
        variances.push_back(plane.temporal_variance_per_pixel_dn2);
      }
    }

    if (means.size() < kMinFitZones) {
      fit.fitted_zone_count = means.size();
      fit.fit_valid = false;
      fit.reason = "insufficient_signal_zones";
      out.dn_referred_ptc_candidate = false;
      continue;
    }

    const auto line = fit_line(means, variances);
    fit.fitted_zone_count = line.n;
    fit.slope_variance_dn2_per_mean_dn = line.slope;
    fit.intercept_variance_dn2 = line.intercept;
    fit.r_squared = line.r_squared;
    fit.fit_valid = line.valid && line.slope > 0.0 && line.r_squared >= 0.90;
    fit.reason = fit.fit_valid ? "ok" : "fit_not_linear_or_nonpositive_slope";
    if (!fit.fit_valid) out.dn_referred_ptc_candidate = false;
  }
  return out;
}

StepchartRawOracleGateDiagnostics evaluate_stepchart_raw_iso_against_oracle(
    const StepchartRawIsoSummary& summary) {
  const auto vectors = oracle_gate_vectors(summary);
  const auto& green = vectors.green;
  const auto& rel_exposure = vectors.rel_exposure;

  const std::size_t n = green.size();
  double mean_g = 0.0;
  double mean_e = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    mean_g += green[i];
    mean_e += rel_exposure[i];
  }
  mean_g /= static_cast<double>(n);
  mean_e /= static_cast<double>(n);
  double num = 0.0;
  double den_g = 0.0;
  double den_e = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double dg = green[i] - mean_g;
    const double de = rel_exposure[i] - mean_e;
    num += dg * de;
    den_g += dg * dg;
    den_e += de * de;
  }

  StepchartRawOracleGateDiagnostics out;
  out.green_monotone_nonincreasing =
      green_is_monotone_nonincreasing(green);
  if (den_e > 0.0) {
    out.linear_gain = num / den_e;
    out.additive_offset = mean_g - out.linear_gain * mean_e;
  }
  if (den_g > 0.0 && den_e > 0.0) {
    out.green_correlation = num / std::sqrt(den_g * den_e);
  }
  out.passes = out.green_monotone_nonincreasing &&
               out.green_correlation >= out.min_green_correlation;
  return out;
}

void validate_stepchart_raw_iso_against_oracle(
    const StepchartRawIsoSummary& summary) {
  // The verdict comes from the diagnostics evaluation — a single source of
  // truth — so the serialized gate block can never disagree with the
  // accept/reject decision. The loops below only shape the error message.
  const auto diagnostics = evaluate_stepchart_raw_iso_against_oracle(summary);
  if (diagnostics.passes) return;

  const auto vectors = oracle_gate_vectors(summary);
  const auto& green = vectors.green;
  const auto& rel_exposure = vectors.rel_exposure;

  // Non-increasing in oracle zone order, with a small tolerance so genuine
  // noise-floor ties in the deepest zones pass.
  for (std::size_t i = 0; i + 1 < green.size(); ++i) {
    const double tolerance = std::max(5.0, 0.02 * green[i]);
    if (green[i + 1] > green[i] + tolerance) {
      throw std::runtime_error(
          "Stepchart raw gate: green zone means are not monotone with the "
          "oracle ladder (zone " + std::to_string(summary.zones[i].zone) +
          " -> " + std::to_string(summary.zones[i + 1].zone) +
          " rises); corner seed or chart-layout model is wrong");
    }
  }

  // A linear sensor tracking the ladder correlates near-perfectly with
  // relative exposure; scene background or misplaced ROIs do not.
  double mean_g = 0.0;
  double mean_e = 0.0;
  for (std::size_t i = 0; i < green.size(); ++i) {
    mean_g += green[i];
    mean_e += rel_exposure[i];
  }
  mean_g /= static_cast<double>(green.size());
  mean_e /= static_cast<double>(green.size());
  double num = 0.0;
  double den_g = 0.0;
  double den_e = 0.0;
  for (std::size_t i = 0; i < green.size(); ++i) {
    const double dg = green[i] - mean_g;
    const double de = rel_exposure[i] - mean_e;
    num += dg * de;
    den_g += dg * dg;
    den_e += de * de;
  }
  if (den_g <= 0.0 || den_e <= 0.0) {
    throw std::runtime_error(
        "Stepchart raw gate: degenerate (constant) zone means do not "
        "correlate with the oracle ladder");
  }
  const double r = num / std::sqrt(den_g * den_e);
  if (r < 0.98) {
    throw std::runtime_error(
        "Stepchart raw gate: green zone means do not correlate with the "
        "oracle exposure ladder (r=" + std::to_string(r) +
        " < 0.98); corner seed or chart-layout model is wrong");
  }
  // The diagnostics rejected but no specific check above fired: non-finite
  // zone means defeat NaN comparisons in the loops. Refuse anyway.
  throw std::runtime_error(
      "Stepchart raw gate: green zone means fail the oracle ladder gate "
      "with non-finite diagnostics");
}

}  // namespace camera_iq
