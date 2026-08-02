#include "camera_iq/spectro_measurement.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::MatArray;
using camera_iq::MatStruct;
using camera_iq::SpectroMeasurement;
using camera_iq::spectro_measurement_from_mat;
using camera_iq::summarize_spectro_repeats;
using test::check;
using test::check_near;

namespace {

MatArray row(std::vector<double> values, bool logical = false) {
  return MatArray{{1, values.size()}, std::move(values), logical};
}

MatArray scalar(double value, bool logical = false) {
  return MatArray{{1, 1}, {value}, logical};
}

MatStruct valid_fields() {
  return {
      {"radiance", row({1.0, -0.5, 3.0})},
      {"wl", row({380.0, 382.0, 384.0})},
      {"XYZ", row({10.0, 20.0, 30.0})},
      {"totalRadiance", scalar(0.25)},
      {"CCT", scalar(5500.0)},
      {"Duv", scalar(-0.001)},
      {"repeatOnError", scalar(1.0, true)},
      {"numCurrentRepetitions", scalar(0.0)},
  };
}

bool conversion_throws(MatStruct fields) {
  try {
    (void)spectro_measurement_from_mat(fields);
    return false;
  } catch (const std::runtime_error&) {
    return true;
  }
}

SpectroMeasurement measurement(double radiance_offset, double xyz_offset = 0.0) {
  auto fields = valid_fields();
  for (double& value : fields.at("radiance").values) value += radiance_offset;
  for (double& value : fields.at("XYZ").values) value += xyz_offset;
  return spectro_measurement_from_mat(fields);
}

}  // namespace

void TESTS() {
  const SpectroMeasurement parsed =
      spectro_measurement_from_mat(valid_fields());
  check(parsed.wavelength_nm == std::vector<double>({380.0, 382.0, 384.0}),
        "spectro record: wavelength axis is retained");
  check(parsed.spectral_radiance == std::vector<double>({1.0, -0.5, 3.0}),
        "spectro record: negative instrument noise is retained");
  check(parsed.recorded_xyz[0] == 10.0 && parsed.recorded_xyz[2] == 30.0,
        "spectro record: recorded XYZ is retained without reinterpretation");
  check_near(parsed.recorded_total_radiance, 0.25, 0.0,
             "spectro record: total radiance metadata is retained");
  check_near(parsed.recorded_cct_k, 5500.0, 0.0,
             "spectro record: CCT is explicitly recorded metadata");
  check_near(parsed.recorded_duv, -0.001, 0.0,
             "spectro record: Duv is explicitly recorded metadata");
  check(parsed.repeat_on_error && parsed.current_repetitions == 0,
        "spectro record: acquisition flags retain their stored semantics");

  const auto repeats =
      summarize_spectro_repeats({measurement(0.0), measurement(2.0, 4.0)});
  check(repeats.count == 2, "repeat summary: reading count is explicit");
  check(repeats.wavelength_nm == parsed.wavelength_nm,
        "repeat summary: validated wavelength axis is retained");
  check_near(repeats.mean_spectral_radiance[0], 2.0, 1e-12,
             "repeat summary: radiance mean is pointwise");
  check(repeats.sample_stddev_spectral_radiance.has_value(),
        "repeat summary: sample spread is present for two readings");
  check_near(repeats.sample_stddev_spectral_radiance->at(0), std::sqrt(2.0),
             1e-12, "repeat summary: radiance spread uses n-1");
  check_near(repeats.mean_recorded_xyz[1], 22.0, 1e-12,
             "repeat summary: recorded XYZ mean is channelwise");
  check_near(repeats.sample_stddev_recorded_xyz->at(1), std::sqrt(8.0),
             1e-12, "repeat summary: recorded XYZ spread uses n-1");

  const auto singleton = summarize_spectro_repeats({parsed});
  check(!singleton.sample_stddev_spectral_radiance.has_value() &&
            !singleton.sample_stddev_recorded_xyz.has_value(),
        "repeat summary: one reading does not fabricate repeatability");

  auto missing = valid_fields();
  missing.erase("radiance");
  check(conversion_throws(missing),
        "spectro record: a missing required field is rejected");

  auto mismatched = valid_fields();
  mismatched.at("wl") = row({380.0, 382.0});
  check(conversion_throws(mismatched),
        "spectro record: wavelength/radiance length mismatch is rejected");

  auto unordered = valid_fields();
  unordered.at("wl") = row({380.0, 384.0, 382.0});
  check(conversion_throws(unordered),
        "spectro record: wavelength axis must be strictly increasing");

  auto nonfinite = valid_fields();
  nonfinite.at("XYZ").values[1] = std::numeric_limits<double>::infinity();
  check(conversion_throws(nonfinite),
        "spectro record: non-finite measurement values are rejected");

  auto not_logical = valid_fields();
  not_logical.at("repeatOnError").logical = false;
  check(conversion_throws(not_logical),
        "spectro record: repeatOnError must retain MATLAB logical semantics");

  auto fractional_count = valid_fields();
  fractional_count.at("numCurrentRepetitions").values = {1.5};
  check(conversion_throws(fractional_count),
        "spectro record: repetition count must be a non-negative integer");

  bool empty_group_threw = false;
  try {
    (void)summarize_spectro_repeats({});
  } catch (const std::runtime_error&) {
    empty_group_threw = true;
  }
  check(empty_group_threw, "repeat summary: empty groups are rejected");

  bool axis_threw = false;
  try {
    SpectroMeasurement shifted = measurement(1.0);
    shifted.wavelength_nm[1] = 383.0;
    (void)summarize_spectro_repeats({parsed, shifted});
  } catch (const std::runtime_error&) {
    axis_threw = true;
  }
  check(axis_threw,
        "repeat summary: readings with different wavelength axes are rejected");

  bool direct_nonfinite_threw = false;
  try {
    SpectroMeasurement invalid = parsed;
    invalid.spectral_radiance[0] =
        std::numeric_limits<double>::quiet_NaN();
    (void)summarize_spectro_repeats({invalid});
  } catch (const std::runtime_error&) {
    direct_nonfinite_threw = true;
  }
  check(direct_nonfinite_threw,
        "repeat summary: directly constructed non-finite readings are rejected");

  SpectroMeasurement high_a = parsed;
  SpectroMeasurement high_b = parsed;
  high_a.spectral_radiance[0] = 1.0e16;
  high_b.spectral_radiance[0] =
      std::nextafter(high_a.spectral_radiance[0],
                     std::numeric_limits<double>::infinity());
  const auto high_summary = summarize_spectro_repeats({high_a, high_b});
  const double exact_two_sample_spread =
      (high_b.spectral_radiance[0] - high_a.spectral_radiance[0]) /
      std::sqrt(2.0);
  check_near(high_summary.sample_stddev_spectral_radiance->at(0),
             exact_two_sample_spread, 1e-12,
             "repeat summary: variance remains accurate under a high offset");

  // Both readings pass the finiteness gate, so the summary must not produce a
  // value that does not. The accumulation subtracts one reading from another
  // in double before any widening, and long double is double on every arm64
  // target -- including this project's development machine -- so widening
  // supplies no headroom there. The true spread here, sqrt(2) * 1e308, is
  // representable; only the intermediate is not.
  SpectroMeasurement wide_low = parsed;
  SpectroMeasurement wide_high = parsed;
  wide_low.spectral_radiance[0] = -1.0e308;
  wide_high.spectral_radiance[0] = 1.0e308;
  const auto wide = summarize_spectro_repeats({wide_low, wide_high});
  check(std::isfinite(wide.mean_spectral_radiance[0]),
        "repeat summary: finite readings produce a finite mean");
  check(wide.sample_stddev_spectral_radiance.has_value() &&
            std::isfinite(wide.sample_stddev_spectral_radiance->at(0)),
        "repeat summary: finite readings produce a finite spread");
  check_near(wide.sample_stddev_spectral_radiance->at(0),
             std::sqrt(2.0) * 1.0e308, 1e-12,
             "repeat summary: the spread of two extreme readings is exact");
}
