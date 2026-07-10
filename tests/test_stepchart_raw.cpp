#include "camera_iq/stepchart_raw.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::ChannelStats;
using camera_iq::ImatestStepchartZone;
using camera_iq::RoiRect;
using camera_iq::StepchartRawFrameMeasurement;
using camera_iq::StepchartRawZoneMeasurement;
using camera_iq::analyze_stepchart_raw_ptc_diagnostic;
using camera_iq::evaluate_stepchart_raw_iso_against_oracle;
using camera_iq::summarize_stepchart_raw_iso;
using camera_iq::validate_stepchart_raw_iso_against_oracle;
using test::check;
using test::check_near;

namespace {

ChannelStats plane(const char* label, double mean, double stddev,
                   double below = 0.0, double saturated = 0.0) {
  ChannelStats p;
  p.label = label;
  p.count = 100;
  p.mean = mean;
  p.stddev = stddev;
  p.below_black_fraction = below;
  p.saturated_fraction = saturated;
  return p;
}

StepchartRawZoneMeasurement zone_measurement(int zone, double base) {
  StepchartRawZoneMeasurement z;
  z.zone = zone;
  z.measurement_roi = RoiRect{10 * zone, 20, 8, 6};
  z.planes = {plane("R", base + 1.0, 1.0, 0.1, 0.0),
              plane("G1", base + 2.0, 2.0, 0.0, 0.2),
              plane("B", base + 3.0, 3.0, 0.0, 0.0),
              plane("G2", base + 4.0, 4.0, 0.0, 0.0)};
  return z;
}

StepchartRawFrameMeasurement frame(const std::string& file, double zone1_base,
                                   double zone2_base) {
  StepchartRawFrameMeasurement f;
  f.file_label = file;
  f.zones = {zone_measurement(1, zone1_base),
             zone_measurement(2, zone2_base)};
  return f;
}

StepchartRawZoneMeasurement sampled_zone_measurement(
    int zone, double mean, double half_delta, double log_exposure = 0.0) {
  (void)log_exposure;
  StepchartRawZoneMeasurement z;
  z.zone = zone;
  z.measurement_roi = RoiRect{10 * zone, 20, 4, 4};
  z.planes = {plane("R", mean, 1.0), plane("G1", mean, 1.0),
              plane("B", mean, 1.0), plane("G2", mean, 1.0)};
  for (auto& samples : z.plane_samples) {
    samples = {mean + half_delta, mean + half_delta, mean + half_delta,
               mean + half_delta};
  }
  return z;
}

StepchartRawFrameMeasurement sampled_frame(
    const std::string& file, const std::vector<double>& means,
    const std::vector<double>& variances, double sign) {
  StepchartRawFrameMeasurement f;
  f.file_label = file;
  for (std::size_t i = 0; i < means.size(); ++i) {
    // Two samples at mean +/- sqrt(var/2) have unbiased sample variance var.
    const double delta = std::sqrt(variances[i] / 2.0) * sign;
    f.zones.push_back(
        sampled_zone_measurement(static_cast<int>(i + 1), means[i], delta));
  }
  return f;
}

std::vector<ImatestStepchartZone> oracle_zones() {
  ImatestStepchartZone z1;
  z1.zone = 1;
  z1.log_exposure = -0.0;
  ImatestStepchartZone z2;
  z2.zone = 2;
  z2.log_exposure = -0.15;
  return {z1, z2};
}

std::vector<ImatestStepchartZone> oracle_zones_for_logs(
    const std::vector<double>& logs) {
  std::vector<ImatestStepchartZone> out;
  for (std::size_t i = 0; i < logs.size(); ++i) {
    ImatestStepchartZone z;
    z.zone = static_cast<int>(i + 1);
    z.log_exposure = logs[i];
    out.push_back(z);
  }
  return out;
}

bool throws_for(const std::vector<StepchartRawFrameMeasurement>& frames) {
  try {
    (void)summarize_stepchart_raw_iso(100, "s1-40", oracle_zones(), frames);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

void TESTS() {
  {
    const auto summary = summarize_stepchart_raw_iso(
        100, "s1-40", oracle_zones(),
        {frame("a.NEF", 10.0, 100.0), frame("b.NEF", 14.0, 104.0)});

    check(summary.iso == 100, "raw stepchart: preserves ISO");
    check(summary.shutter_token == "s1-40",
          "raw stepchart: preserves shutter token");
    check(summary.frame_count == 2, "raw stepchart: frame count");
    check(summary.zones.size() == 2, "raw stepchart: zone count");
    check(summary.zones[0].zone == 1 && summary.zones[1].zone == 2,
          "raw stepchart: zones remain ordered by oracle");
    check_near(summary.zones[1].log_exposure, -0.15, 1e-12,
               "raw stepchart: carries oracle log exposure");
    check(summary.zones[0].measurement_roi.has_value(),
          "raw stepchart: repeated identical actual ROI is serialized");
    check(summary.zones[0].measurement_roi->x == 10,
          "raw stepchart: ROI x is preserved");

    const auto& r = summary.zones[0].planes[0];
    check(r.channel == "R", "raw stepchart: plane label preserved");
    check(r.frame_count == 2, "raw stepchart: plane frame count");
    check_near(r.mean_dn, 13.0, 1e-12,
               "raw stepchart: mean DN is average of repeat ROI means");
    check_near(r.temporal_stddev_of_zone_mean_dn, 2.0, 1e-12,
               "raw stepchart: repeat spread is population stddev of ROI means");
    check_near(r.mean_spatial_stddev_dn, 1.0, 1e-12,
               "raw stepchart: spatial stddev averaged across repeats");
    check_near(r.max_below_black_fraction, 0.1, 1e-12,
               "raw stepchart: below-black fraction rollup");
    check_near(summary.zones[0].planes[1].max_saturated_fraction, 0.2, 1e-12,
               "raw stepchart: saturation fraction rollup");

    check(!summary.ptc_candidate,
          "raw stepchart: repeat ROI spread is not promoted to PTC");
    check(!summary.dynamic_range_candidate,
          "raw stepchart: no engineering DR claim");
    check(!summary.limitations.empty(),
          "raw stepchart: limitations explain DN-only scope");
  }

  {
    StepchartRawFrameMeasurement missing = frame("a.NEF", 10.0, 100.0);
    missing.zones.pop_back();
    check(throws_for({missing, frame("b.NEF", 14.0, 104.0)}),
          "raw stepchart: rejects frames missing an oracle zone");
  }

  {
    auto duplicate = frame("a.NEF", 10.0, 100.0);
    duplicate.zones.push_back(zone_measurement(2, 200.0));
    check(throws_for({duplicate, frame("b.NEF", 14.0, 104.0)}),
          "raw stepchart: rejects duplicate zones in a frame");
  }

  {
    auto changed_roi_a = frame("a.NEF", 10.0, 100.0);
    auto changed_roi_b = frame("b.NEF", 14.0, 104.0);
    changed_roi_b.zones[0].measurement_roi = RoiRect{99, 20, 8, 6};
    const auto summary = summarize_stepchart_raw_iso(
        100, "s1-40", oracle_zones(), {changed_roi_a, changed_roi_b});
    check(!summary.zones[0].measurement_roi.has_value(),
          "raw stepchart: varying clipped ROI is marked null");
  }

  {
    auto a = frame("a.NEF", 100.0, 200.0);
    auto b = frame("b.NEF", 100.0, 200.0);
    a.zones[0].plane_samples[1] = {90.0, 110.0};
    b.zones[0].plane_samples[1] = {110.0, 90.0};
    a.zones[0].plane_samples[2] = {90.0, 110.0};
    b.zones[0].plane_samples[2] = {110.0, 90.0};
    const auto summary =
        summarize_stepchart_raw_iso(100, "s1-40", oracle_zones(), {a, b});
    check_near(summary.zones[0].planes[1].temporal_stddev_of_zone_mean_dn,
               0.0, 1e-12,
               "raw stepchart PTC: ROI-mean spread can be zero");
    check(summary.zones[0].planes[1].aligned_pixel_count == 2,
          "raw stepchart PTC: aligned pixel count emitted");
    check_near(summary.zones[0].planes[1].temporal_variance_per_pixel_dn2,
               200.0, 1e-12,
               "raw stepchart PTC: per-pixel temporal sample variance is measured");
    check_near(summary.zones[0].planes[1].temporal_stddev_per_pixel_dn,
               std::sqrt(200.0), 1e-12,
               "raw stepchart PTC: per-pixel temporal stddev is measured");
  }

  {
    auto a = frame("a.NEF", 100.0, 200.0);
    auto b = frame("b.NEF", 100.0, 200.0);
    a.zones[0].plane_samples[1] = {90.0, 110.0};
    b.zones[0].plane_samples[1] = {110.0};
    check(throws_for({a, b}),
          "raw stepchart PTC: rejects mismatched aligned sample counts");
  }

  {
    auto a = frame("a.NEF", 100.0, 200.0);
    auto b = frame("b.NEF", 100.0, 200.0);
    a.zones[0].plane_samples[1] = {90.0, 110.0};
    b.zones[0].plane_samples[1] = {110.0, 90.0};
    b.zones[0].measurement_roi = RoiRect{12, 20, 8, 6};
    check(throws_for({a, b}),
          "raw stepchart PTC: rejects non-identical ROI for aligned samples");
  }

  {
    const std::vector<double> means{1000, 800, 600, 400, 320};
    std::vector<double> variances;
    for (double mean : means) variances.push_back(0.5 * mean + 4.0);
    auto summary = summarize_stepchart_raw_iso(
        100, "s1-40", oracle_zones_for_logs({0, -0.1, -0.2, -0.3, -0.4}),
        {sampled_frame("a.NEF", means, variances, -1.0),
         sampled_frame("b.NEF", means, variances, 1.0)});
    const auto diagnostic = analyze_stepchart_raw_ptc_diagnostic(summary);
    check(diagnostic.dn_referred_ptc_candidate,
          "raw stepchart PTC: diagnostic is candidate when per-pixel variances fit");
    check(!diagnostic.electron_calibrated_gain_candidate,
          "raw stepchart PTC: electron gain is not claimed");
    check(!diagnostic.dynamic_range_candidate,
          "raw stepchart PTC: dynamic range is not claimed");
    const auto& g1 = diagnostic.planes[1];
    check(g1.fit_valid, "raw stepchart PTC: G1 fit is valid");
    check(g1.fitted_zone_count == 5,
          "raw stepchart PTC: fit records included zone count");
    check_near(g1.slope_variance_dn2_per_mean_dn, 0.5, 1e-12,
               "raw stepchart PTC: slope is recovered");
    check_near(g1.intercept_variance_dn2, 4.0, 1e-9,
               "raw stepchart PTC: intercept is recovered");
    check_near(g1.r_squared, 1.0, 1e-12,
               "raw stepchart PTC: R^2 is emitted");
    check(summary.zones[0].planes[1].ptc_fit_included,
          "raw stepchart PTC: included zone is marked");
  }

  // Empirical oracle-ladder gate: extracted green means must actually follow
  // the oracle exposure ladder, or the extraction (corner seed / chart-layout
  // model) is wrong and the command must refuse rather than emit garbage.
  {
    auto ladder_summary = [](const std::vector<double>& green_means,
                             const std::vector<double>& log_exposures) {
      camera_iq::StepchartRawIsoSummary s2;
      s2.iso = 100;
      for (std::size_t i = 0; i < green_means.size(); ++i) {
        camera_iq::StepchartRawZoneSummary z;
        z.zone = static_cast<int>(i + 1);
        z.log_exposure = log_exposures[i];
        for (auto& p2 : z.planes) {
          p2.channel = "R";
          p2.mean_dn = green_means[i] * 0.8;
        }
        z.planes[1].channel = "G1";
        z.planes[1].mean_dn = green_means[i];
        z.planes[2].channel = "G2";
        z.planes[2].mean_dn = green_means[i];
        s2.zones.push_back(z);
      }
      return s2;
    };
    auto gate_throws = [](const camera_iq::StepchartRawIsoSummary& s2,
                          const std::string& needle) {
      try {
        validate_stepchart_raw_iso_against_oracle(s2);
      } catch (const std::runtime_error& e) {
        return std::string(e.what()).find(needle) != std::string::npos;
      }
      return false;
    };

    // A linear sensor tracking the ladder passes (deep-shadow ties allowed).
    const std::vector<double> log_e{0.0,   -0.3,  -0.6,  -0.9, -1.2,
                                    -1.6,  -2.0,  -2.6,  -3.2, -4.1};
    std::vector<double> good;
    for (double le : log_e) good.push_back(12000.0 * std::pow(10.0, le));
    good[8] = good[9];  // noise-floor tie in the deepest zones.
    validate_stepchart_raw_iso_against_oracle(ladder_summary(good, log_e));
    check(true, "raw stepchart gate: oracle-consistent ladder accepted");

    const auto exact = ladder_summary({12010.0, 1210.0, 130.0},
                                      {0.0, -1.0, -2.0});
    const auto diag = evaluate_stepchart_raw_iso_against_oracle(exact);
    check(diag.green_monotone_nonincreasing,
          "raw stepchart gate diagnostics: monotone flag emitted");
    check_near(diag.green_correlation, 1.0, 1e-12,
               "raw stepchart gate diagnostics: correlation emitted");
    check_near(diag.min_green_correlation, 0.98, 1e-12,
               "raw stepchart gate diagnostics: threshold emitted");
    check_near(diag.linear_gain, 12000.0, 1e-9,
               "raw stepchart gate diagnostics: linear gain emitted");
    check_near(diag.additive_offset, 10.0, 1e-9,
               "raw stepchart gate diagnostics: additive offset emitted");
    check(diag.passes, "raw stepchart gate diagnostics: pass flag emitted");

    // A mid-ladder reversal (ROI landed on the wrong physical patch) fails.
    std::vector<double> reversed = good;
    reversed[5] = reversed[2] * 1.5;
    check(gate_throws(ladder_summary(reversed, log_e), "monotone"),
          "raw stepchart gate: rejects non-monotone zone means");

    // Monotone but flat/uncorrelated means (background, not the chart) fail.
    std::vector<double> flat;
    for (std::size_t i = 0; i < log_e.size(); ++i) {
      flat.push_back(600.0 - static_cast<double>(i));
    }
    check(gate_throws(ladder_summary(flat, log_e), "correlate"),
          "raw stepchart gate: rejects means uncorrelated with the ladder");

    // Failing summaries must be visible in the serialized diagnostics too.
    const auto reversed_diag = evaluate_stepchart_raw_iso_against_oracle(
        ladder_summary(reversed, log_e));
    check(!reversed_diag.green_monotone_nonincreasing,
          "raw stepchart gate diagnostics: reversal clears monotone flag");
    check(!reversed_diag.passes,
          "raw stepchart gate diagnostics: reversal clears pass flag");

    const auto flat_diag =
        evaluate_stepchart_raw_iso_against_oracle(ladder_summary(flat, log_e));
    check(flat_diag.green_correlation < 0.98,
          "raw stepchart gate diagnostics: background correlation below gate");
    check(!flat_diag.passes,
          "raw stepchart gate diagnostics: background clears pass flag");

    // Non-finite zone means must refuse, not sneak past NaN comparisons.
    std::vector<double> poisoned = good;
    poisoned[3] = std::numeric_limits<double>::quiet_NaN();
    check(gate_throws(ladder_summary(poisoned, log_e), "oracle"),
          "raw stepchart gate: rejects non-finite zone means");
  }
}
