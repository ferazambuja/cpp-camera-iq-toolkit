#include "camera_iq/sampled_spectrum.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::SampledSpectrum;
using camera_iq::analyze_sampled_spectrum_group;
using test::check;
using test::check_near;

namespace {

SampledSpectrum spectrum(double scale) {
  return SampledSpectrum{{380.0, 382.0, 384.0},
                         {1.0 * scale, 2.0 * scale, 1.0 * scale}};
}
template <typename Operation>
bool throws_with(Operation operation, const std::string& needle) {
  try {
    operation();
  } catch (const std::runtime_error& error) {
    return std::string(error.what()).find(needle) != std::string::npos;
  }
  return false;
}

}  // namespace

void TESTS() {
  const auto result =
      analyze_sampled_spectrum_group({spectrum(1.0), spectrum(2.0)});
  check(result.count == 2 && result.wavelength_step_nm == 2.0,
        "sampled spectrum: group size and native step are retained");
  check(result.sample_weighting == "uniform_equal_weight",
        "sampled spectrum: integration rule is explicit");
  check_near(result.readings[0].spectral_integral, 8.0, 1e-12,
             "sampled spectrum: integral includes sample width");
  check_near(result.mean_spectral_integral, 12.0, 1e-12,
             "sampled spectrum: absolute level is separate from shape");
  check(result.coefficient_of_variation.has_value(),
        "sampled spectrum: repeated levels carry a sample CV");
  check_near(*result.coefficient_of_variation, std::sqrt(32.0) / 12.0,
             1e-12, "sampled spectrum: CV uses n-1 sample deviation");
  check(result.mean_normalized_spectrum ==
            std::vector<double>({0.125, 0.25, 0.125}),
        "sampled spectrum: group mean is formed after normalization");
  check(result.max_shape_relative_l2.has_value() &&
            *result.max_shape_relative_l2 == 0.0,
        "sampled spectrum: scale-only variation has zero shape residual");

  const auto singleton = analyze_sampled_spectrum_group({spectrum(1.0)});
  check(!singleton.sample_stddev_spectral_integral.has_value() &&
            !singleton.coefficient_of_variation.has_value() &&
            !singleton.sample_stddev_normalized_spectrum.has_value() &&
            !singleton.max_shape_relative_l2.has_value(),
        "sampled spectrum: singleton variation is absent");

  check(throws_with([] { (void)analyze_sampled_spectrum_group({}); },
                    "group is empty"),
        "sampled spectrum: empty group is refused");
  check(throws_with(
            [] {
              auto invalid = spectrum(1.0);
              invalid.wavelength_nm = {380.0};
              invalid.values = {1.0};
              (void)analyze_sampled_spectrum_group({invalid});
            },
            "grid is too short"),
        "sampled spectrum: trivial grid is refused");
  check(throws_with(
            [] {
              auto invalid = spectrum(1.0);
              invalid.wavelength_nm[2] = 385.0;
              (void)analyze_sampled_spectrum_group({invalid});
            },
            "uniform grid"),
        "sampled spectrum: non-uniform grid is refused");
  check(throws_with(
            [] {
              auto second = spectrum(1.0);
              second.wavelength_nm[1] = 381.0;
              (void)analyze_sampled_spectrum_group({spectrum(1.0), second});
            },
            "share one wavelength grid"),
        "sampled spectrum: mismatched repeat axes are refused");
  check(throws_with(
            [] {
              auto invalid = spectrum(1.0);
              invalid.values[1] = std::numeric_limits<double>::infinity();
              (void)analyze_sampled_spectrum_group({invalid});
            },
            "values must be finite"),
        "sampled spectrum: non-finite samples are refused");
  check(throws_with(
            [] {
              auto invalid = spectrum(1.0);
              invalid.values = {1.0, -2.0, 1.0};
              (void)analyze_sampled_spectrum_group({invalid});
            },
            "integral must be finite and positive"),
        "sampled spectrum: non-positive normalization is refused");
}
