#include "camera_iq/spectro_analysis.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include "harness.hpp"

using camera_iq::analyze_spectro_group;
using camera_iq::SpectroMeasurement;
using test::check;
using test::check_near;

namespace {

SpectroMeasurement reading(double scale) {
  SpectroMeasurement value;
  value.wavelength_nm = {380.0, 382.0, 384.0};
  value.spectral_radiance = {1.0 * scale, 2.0 * scale, 1.0 * scale};
  value.recorded_xyz = {10.0 * scale, 20.0 * scale, 30.0 * scale};
  return value;
}

} // namespace

void TESTS() {
  const auto same_shape = analyze_spectro_group({reading(1.0), reading(2.0)});
  check(same_shape.count == 2 && same_shape.wavelength_step_nm == 2.0,
        "spectro analysis: the declared grid and group size are retained");
  check(same_shape.sample_weighting == "uniform_equal_weight",
        "spectro analysis: the integration rule is explicit");
  check_near(same_shape.readings[0].spectral_integral, 8.0, 1e-12,
             "spectro analysis: integral includes the 2 nm sample width");
  check_near(same_shape.mean_spectral_integral, 12.0, 1e-12,
             "spectro analysis: absolute level is summarized separately");
  check(same_shape.sample_stddev_spectral_integral.has_value() &&
            same_shape.coefficient_of_variation.has_value(),
        "spectro analysis: repeated measurements carry level variation");
  check_near(*same_shape.coefficient_of_variation, std::sqrt(32.0) / 12.0,
             1e-12, "spectro analysis: level CV uses the n-1 sample deviation");
  check(same_shape.mean_normalized_spectrum ==
            std::vector<double>({0.125, 0.25, 0.125}),
        "spectro analysis: every spectrum is normalized before averaging");
  check(same_shape.max_shape_relative_l2.has_value() &&
            *same_shape.max_shape_relative_l2 == 0.0,
        "spectro analysis: pure level scaling has zero shape residual");
  check(same_shape.max_pair_delta_u_prime_v_prime.has_value() &&
            *same_shape.max_pair_delta_u_prime_v_prime == 0.0,
        "spectro analysis: proportional XYZ has zero chromaticity separation");

  const auto singleton = analyze_spectro_group({reading(1.0)});
  check(!singleton.sample_stddev_spectral_integral.has_value() &&
            !singleton.coefficient_of_variation.has_value() &&
            !singleton.sample_stddev_normalized_spectrum.has_value() &&
            !singleton.max_shape_relative_l2.has_value() &&
            !singleton.max_pair_delta_u_prime_v_prime.has_value(),
        "spectro analysis: singleton variation metrics are absent");

  SpectroMeasurement changed_chromaticity = reading(2.0);
  changed_chromaticity.recorded_xyz[1] += 1.0;
  const auto chromatic =
      analyze_spectro_group({reading(1.0), changed_chromaticity});
  check(chromatic.max_pair_delta_u_prime_v_prime.has_value() &&
            *chromatic.max_pair_delta_u_prime_v_prime > 0.0,
        "spectro analysis: nonzero chromaticity variation is retained");

  SpectroMeasurement high_level_a = reading(1.0);
  high_level_a.wavelength_nm = {1.0, 2.0};
  high_level_a.spectral_radiance = {5.0e307, 5.0e307};
  SpectroMeasurement high_level_b = high_level_a;
  high_level_b.spectral_radiance = {2.5e307, 2.5e307};
  const auto high_level = analyze_spectro_group({high_level_a, high_level_b});
  check(high_level.sample_stddev_spectral_integral.has_value() &&
            std::isfinite(*high_level.sample_stddev_spectral_integral),
        "spectro analysis: representable high-level spread remains finite");
  check_near(*high_level.sample_stddev_spectral_integral / 1.0e307,
             2.5 * std::sqrt(2.0), 1e-12,
             "spectro analysis: high-level spread avoids squared overflow");

  SpectroMeasurement cancellation_integral = reading(1.0);
  cancellation_integral.wavelength_nm = {1.0, 2.0, 3.0};
  const double three_quarters_max =
      0.75 * std::numeric_limits<double>::max();
  cancellation_integral.spectral_radiance = {
      three_quarters_max, three_quarters_max, -three_quarters_max};
  const auto cancellation_result =
      analyze_spectro_group({cancellation_integral});
  check(std::isfinite(cancellation_result.readings[0].spectral_integral) &&
            cancellation_result.readings[0].spectral_integral ==
                three_quarters_max,
        "spectro analysis: a representable integral survives intermediate "
        "summation overflow");

  SpectroMeasurement lowest_level = reading(1.0);
  lowest_level.wavelength_nm = {1.0, 2.0};
  lowest_level.spectral_radiance = {
      std::numeric_limits<double>::denorm_min(), 0.0};
  const auto subnormal =
      analyze_spectro_group({lowest_level, lowest_level});
  check(subnormal.mean_spectral_integral ==
            std::numeric_limits<double>::denorm_min(),
        "spectro analysis: the mean retains a positive subnormal level");
  check(subnormal.sample_stddev_spectral_integral.has_value() &&
            *subnormal.sample_stddev_spectral_integral == 0.0 &&
            subnormal.coefficient_of_variation.has_value() &&
            *subnormal.coefficient_of_variation == 0.0,
        "spectro analysis: identical subnormal levels have finite zero "
        "variation");

  SpectroMeasurement subnormal_shape = reading(1.0);
  subnormal_shape.wavelength_nm = {1.0, 2.0};
  subnormal_shape.spectral_radiance = {
      std::numeric_limits<double>::denorm_min(), 1.0};
  const auto shape_underflow =
      analyze_spectro_group({subnormal_shape, subnormal_shape});
  check(shape_underflow.mean_normalized_spectrum[0] ==
            std::numeric_limits<double>::denorm_min(),
        "spectro analysis: the normalized mean retains a representable "
        "subnormal sample");
  check(shape_underflow.sample_stddev_normalized_spectrum.has_value() &&
            (*shape_underflow.sample_stddev_normalized_spectrum)[0] == 0.0 &&
            shape_underflow.max_shape_relative_l2.has_value() &&
            *shape_underflow.max_shape_relative_l2 == 0.0,
        "spectro analysis: identical subnormal shapes have zero variation");

  SpectroMeasurement largest_xyz = reading(1.0);
  largest_xyz.recorded_xyz = {std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::max(),
                              std::numeric_limits<double>::max()};
  const auto scaled_chromaticity = analyze_spectro_group({largest_xyz});
  const auto &largest_chromaticity =
      scaled_chromaticity.readings[0].recorded_xyz_chromaticity;
  check_near(largest_chromaticity.x, 1.0 / 3.0, 1e-15,
             "spectro analysis: chromaticity avoids a representable XYZ-sum "
             "overflow");
  check_near(largest_chromaticity.u_prime, 4.0 / 19.0, 1e-15,
             "spectro analysis: u-prime avoids a representable denominator "
             "overflow");
  check_near(largest_chromaticity.v_prime, 9.0 / 19.0, 1e-15,
             "spectro analysis: v-prime avoids a representable denominator "
             "overflow");

  bool normalization_overflow_threw = false;
  try {
    SpectroMeasurement cancellation = reading(1.0);
    cancellation.wavelength_nm = {1.0, 2.0, 3.0};
    cancellation.spectral_radiance = {1.0e308, -1.0e308, 1.0e-308};
    (void)analyze_spectro_group({cancellation});
  } catch (const std::runtime_error &) {
    normalization_overflow_threw = true;
  }
  check(normalization_overflow_threw,
        "spectro analysis: non-representable normalization is refused");
}
