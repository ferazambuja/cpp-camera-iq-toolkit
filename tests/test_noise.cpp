#include "camera_iq/noise.hpp"

#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::NoisePairEstimate;
using camera_iq::RawCfaImage;
using camera_iq::RoiRect;
using camera_iq::compute_noise_pair_estimate;
using camera_iq::fit_dark_current_diagnostic;
using camera_iq::validate_noise_pair_compatibility;
using camera_iq::write_noise_json;
using test::check;
using test::check_near;

namespace {

RawCfaImage image_from_planes(int width, int height,
                              const std::array<std::vector<double>, 4>& planes) {
  RawCfaImage image;
  image.width = width;
  image.height = height;
  image.row_stride_pixels = width;
  image.cdesc = "RGBG";
  image.color_at_position = {0, 1, 2, 3};
  image.meta.cfa_pattern = "RGGB";
  image.meta.iso = 200;
  image.meta.aperture = 8;
  image.meta.shutter_s = 1.0 / 60.0;
  image.samples.assign(static_cast<std::size_t>(width * height), 0.0);
  std::array<std::size_t, 4> idx{0, 0, 0, 0};
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const std::size_t p = static_cast<std::size_t>((r & 1) * 2 + (c & 1));
      image.samples[static_cast<std::size_t>(r * width + c)] =
          planes[p][idx[p]++];
    }
  }
  return image;
}

std::array<std::vector<double>, 4> repeat_planes(
    const std::vector<double>& values) {
  return {values, values, values, values};
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

void TESTS() {
  // Eight samples per plane, with fixed pattern P and deterministic anti-phase
  // temporal offsets.  The frame difference has population stddev 4 DN, so
  // stddev(f1-f2)/sqrt(2) is 2*sqrt(2) DN and the DSNU correction is material.
  const std::vector<double> pattern{-3, -2, -1, 0, 1, 2, 3, 4};
  const std::vector<double> noise{2, -2, 2, -2, 2, -2, 2, -2};
  std::vector<double> first_values;
  std::vector<double> second_values;
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    first_values.push_back(pattern[i] + noise[i]);
    second_values.push_back(pattern[i] - noise[i]);
  }
  const RawCfaImage first = image_from_planes(4, 8, repeat_planes(first_values));
  const RawCfaImage second = image_from_planes(4, 8, repeat_planes(second_values));
  const NoisePairEstimate estimate = compute_noise_pair_estimate(
      first, second, "f1.RAF", "f2.RAF", "1:60", 1.0 / 60.0, 200, 8.0);

  check(estimate.planes[0].channel == "R", "noise labels R plane");
  check(estimate.planes[1].channel == "G1", "noise labels first green plane");
  check(estimate.planes[2].channel == "B", "noise labels B plane");
  check(estimate.planes[3].channel == "G2", "noise labels second green plane");
  check_near(estimate.planes[0].temporal_noise_dn, 2.0 * std::sqrt(2.0), 1e-12,
             "temporal noise is stddev difference divided by sqrt2");
  check_near(estimate.planes[0].pair_mean_spatial_stddev_dn,
             std::sqrt(5.25), 1e-12, "pair mean retains fixed pattern stddev");
  check(estimate.planes[0].dsnu_moment_dn.has_value(),
        "positive DSNU moment is emitted");
  check_near(*estimate.planes[0].dsnu_moment_dn, std::sqrt(1.25), 1e-12,
             "DSNU subtracts temporal variance over N=2");
  // Robust MAD DSNU must subtract the SAME temporal floor as the moment
  // estimate; otherwise it is not a DSNU and is not comparable to the moment
  // column.  pair-mean MAD here is 2*1.4826; robust^2 = mad^2 - temporal^2/2.
  const double material_mad = 2.0 * 1.4826;
  check(estimate.planes[0].dsnu_robust_mad_dn.has_value(),
        "positive robust DSNU is emitted");
  check_near(*estimate.planes[0].dsnu_robust_mad_dn,
             std::sqrt(material_mad * material_mad - 4.0), 1e-9,
             "robust DSNU subtracts temporal variance over N=2");
  check(*estimate.planes[0].dsnu_robust_mad_dn <
            estimate.planes[0].pair_mean_mad_stddev_dn,
        "temporal correction lowers robust DSNU below raw pair-mean MAD");
  check(estimate.planes[0].dsnu_robust_reason == "ok",
        "positive robust DSNU has ok reason");

  const RawCfaImage identical =
      image_from_planes(4, 8, repeat_planes(pattern));
  const NoisePairEstimate no_temporal = compute_noise_pair_estimate(
      identical, identical, "same1.RAF", "same2.RAF", "1:60", 1.0 / 60.0,
      200, 8.0);
  check_near(no_temporal.planes[0].temporal_noise_dn, 0.0, 1e-12,
             "identical frames have zero temporal noise");
  check(no_temporal.planes[0].dsnu_moment_dn.has_value(),
        "identical frames emit DSNU");
  check_near(*no_temporal.planes[0].dsnu_moment_dn, std::sqrt(5.25), 1e-12,
             "identical frames DSNU equals spatial fixed-pattern stddev");

  const std::vector<double> high_noise{10, -10, 10, -10, 10, -10, 10, -10};
  std::vector<double> high_a, high_b;
  for (double n : high_noise) {
    high_a.push_back(n);
    high_b.push_back(-n);
  }
  const NoisePairEstimate below_floor = compute_noise_pair_estimate(
      image_from_planes(4, 8, repeat_planes(high_a)),
      image_from_planes(4, 8, repeat_planes(high_b)), "hot1.RAF", "hot2.RAF",
      "1:60", 1.0 / 60.0, 200, 8.0);
  check(!below_floor.planes[0].dsnu_moment_dn.has_value(),
        "negative DSNU variance is clamped to null");
  check(below_floor.planes[0].dsnu_moment_reason ==
            "dsnu_below_temporal_floor",
        "negative DSNU clamp has machine-readable reason");
  check(!below_floor.planes[0].dsnu_robust_mad_dn.has_value(),
        "robust DSNU also clamps to null below the temporal floor");
  check(below_floor.planes[0].dsnu_robust_reason ==
            "dsnu_below_temporal_floor",
        "robust DSNU clamp has machine-readable reason");

  std::vector<double> hot_pattern = pattern;
  hot_pattern.back() = 1000.0;
  const NoisePairEstimate hot = compute_noise_pair_estimate(
      image_from_planes(4, 8, repeat_planes(hot_pattern)),
      image_from_planes(4, 8, repeat_planes(hot_pattern)), "hp1.RAF",
      "hp2.RAF", "1:60", 1.0 / 60.0, 200, 8.0);
  check(hot.planes[0].pair_mean_spatial_stddev_dn > 300.0,
        "moment DSNU is hot-pixel inclusive");
  check(hot.planes[0].dsnu_robust_mad_dn.has_value() &&
            *hot.planes[0].dsnu_robust_mad_dn < 6.0,
        "robust MAD DSNU is stable against one hot pixel");

  RawCfaImage mismatched = identical;
  mismatched.width = 2;
  check(validate_noise_pair_compatibility(identical, mismatched).has_value(),
        "dimension mismatch is rejected");
  mismatched = identical;
  mismatched.color_at_position = {1, 0, 2, 3};
  check(validate_noise_pair_compatibility(identical, mismatched).has_value(),
        "CFA phase mismatch is rejected");
  mismatched = identical;
  mismatched.samples.pop_back();
  check(validate_noise_pair_compatibility(identical, mismatched).has_value(),
        "sample buffer mismatch is rejected");

  const auto fits = fit_dark_current_diagnostic(
      {{0.01, {0.02, 0.03, 0.04, 0.05}},
       {0.02, {0.02, 0.03, 0.04, 0.05}},
       {0.04, {0.02, 0.03, 0.04, 0.05}}},
      {"R", "G1", "B", "G2"});
  check(!fits[0].measurable, "flat dark-current fit is not measurable");
  check(fits[0].reason == "dark_current_not_measurable_from_ladder",
        "flat dark-current fit has reason");

  camera_iq::NoiseSummary summary;
  summary.root_label = "dataset:fixture/Images/Dark Frame";
  summary.candidate_frames = 3;
  summary.readable_frames = 2;
  summary.in_tolerance_frames = 2;
  summary.excluded_frames = 1;
  summary.matched_pair_count = 1;
  summary.single_pair_only = true;
  summary.gain_candidate = false;
  summary.ptc_candidate = false;
  summary.dr_candidate = false;
  summary.gain_not_supported_reason =
      "no_uniform_flat_exposure_stack_for_gain";
  summary.ptc_not_supported_reason =
      "no_repeated_uniform_flat_ladder_for_photon_transfer";
  summary.dr_not_supported_reason =
      "requires_gain_and_full_well_after_ptc";
  summary.pairs.push_back(estimate);
  summary.dark_current_fits = fits;
  summary.exclusions.push_back({"bad.RAF", "dark_calibration_outlier", 81.2});

  std::ostringstream os;
  write_noise_json(os, summary);
  const std::string doc = os.str();
  check(contains(doc, "\"mode\":\"noise\""), "noise json mode");
  check(contains(doc, "\"unit\":\"DN\""), "noise json records DN unit");
  check(contains(doc, "\"single_pair_only\":true"),
        "noise json records single-pair status");
  check(contains(doc, "\"gain_candidate\":false"),
        "noise json refuses gain candidate");
  check(contains(doc, "\"ptc_candidate\":false"),
        "noise json refuses PTC candidate");
  check(contains(doc, "\"dr_candidate\":false"),
        "noise json refuses DR candidate");
  check(contains(doc, "\"dsnu_below_temporal_floor\"") ||
            contains(doc, "\"dsnu_moment_dn\""),
        "noise json emits DSNU field or clamp reason");
  check(contains(doc, "\"dark_calibration_outlier\""),
        "noise json emits excluded frame reason");
}
