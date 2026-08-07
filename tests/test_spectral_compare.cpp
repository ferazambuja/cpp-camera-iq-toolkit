#include "camera_iq/spectral_compare.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::SampledSpectrum;
using camera_iq::SpectralOffsetSeries;
using camera_iq::SpectralComparisonOptions;
using camera_iq::compare_spectral_groups;
using test::check;
using test::check_near;

namespace {

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
  const std::vector<SampledSpectrum> reference = {
      {{0.0, 1.0, 2.0}, {1.0, 2.0, 1.0}}};
  const std::vector<SampledSpectrum> candidate = {
      {{0.0, 2.0}, {2.0, 2.0}}};
  SpectralComparisonOptions options;
  options.common_wavelength_nm = {0.0, 1.0, 2.0};
  options.excluded_wavelength_nm = {1.0};
  const auto compared = compare_spectral_groups(reference, candidate, options);
  check(!compared.zero_offset_objective.has_value(),
        "spectral compare: zero-offset baseline is absent without a sweep");
  CAMERA_IQ_DOC_EVIDENCE(
      spectral_crosscheck_common_grid,
      check(compared.normalization == "common_grid_equal_weight_integral" &&
                compared.interpolation == "linear" &&
                compared.relative_l2_denominator == "reference_l2_norm" &&
                compared.offset_objective_scope ==
                    "per_offset_equal_weight_integral_normalization_on_fixed_common_grid",
            "spectral compare: method choices are serialized concepts"));
  CAMERA_IQ_DOC_EVIDENCE(
      spectral_crosscheck_common_grid,
      check(compared.reference_on_common_grid ==
                std::vector<double>({0.25, 0.5, 0.25}) &&
                std::all_of(compared.candidate_on_common_grid.begin(),
                            compared.candidate_on_common_grid.end(),
                            [](double value) {
                              return std::abs(value - 1.0 / 3.0) < 1e-12;
                            }),
            "spectral compare: means are resampled before common-grid normalization"));
  CAMERA_IQ_DOC_EVIDENCE(
      spectral_crosscheck_common_grid,
      check_near(compared.directional_relative_l2, 1.0 / 3.0, 1e-12,
                 "spectral compare: residual uses the named reference norm"));
  check_near(compared.bands[1].squared_residual_fraction, 2.0 / 3.0,
             1e-12,
        "spectral compare: band contribution localizes the discrepancy");
  check(compared.exclusion_results.size() == 1 &&
            std::abs(compared.exclusion_results[0].directional_relative_l2 -
                     1.0 / 3.0) < 1e-12,
        "spectral compare: diagnostic exclusion is reported separately");

  SpectralComparisonOptions shifted;
  shifted.common_wavelength_nm = {0.0, 1.0, 2.0, 3.0, 4.0};
  shifted.offset_min_nm = -1.0;
  shifted.offset_max_nm = 1.0;
  shifted.offset_step_nm = 1.0;
  const std::vector<SampledSpectrum> centred = {
      {{0.0, 1.0, 2.0, 3.0, 4.0}, {1.0, 1.0, 3.0, 1.0, 1.0}}};
  const std::vector<SampledSpectrum> displaced = {
      {{0.0, 1.0, 2.0, 3.0, 4.0}, {1.0, 1.0, 1.0, 3.0, 1.0}}};
  const auto sweep = compare_spectral_groups(centred, displaced, shifted);
  check(sweep.offset_sensitivity.size() == 3 &&
            sweep.offset_common_grid_sample_count == 3,
        "spectral compare: offset sweep uses one common supported interior");
  check(sweep.zero_offset_objective.has_value(),
        "spectral compare: sweep reports a zero-offset baseline on its fixed grid");
  check_near(sweep.zero_offset_objective->directional_relative_l2,
             sweep.offset_sensitivity[1].objective.directional_relative_l2,
             0.0,
             "spectral compare: zero-offset baseline matches the sampled zero row");
  check_near(sweep.zero_offset_objective->residual_l2_norm,
             std::sqrt(8.0) / 5.0, 1e-12,
             "spectral compare: zero offset exposes the residual norm");
  check_near(sweep.zero_offset_objective->reference_l2_norm,
             std::sqrt(11.0) / 5.0, 1e-12,
             "spectral compare: zero offset exposes the moving denominator");
  check(std::abs(sweep.zero_offset_objective->directional_relative_l2 -
                 sweep.directional_relative_l2) > 1e-6,
        "spectral compare: trimmed sweep baseline is distinct from the full-grid result");
  check_near(sweep.best_wavelength_offset_nm, -1.0, 1e-12,
             "spectral compare: signed offset convention is explicit");
  check_near(sweep.best_offset_directional_relative_l2, 0.0, 1e-12,
             "spectral compare: aligned feature reaches zero residual");
  check(sweep.best_offset_bands.size() == 3 &&
            std::all_of(sweep.best_offset_bands.begin(),
                        sweep.best_offset_bands.end(), [](const auto& band) {
                          return band.squared_residual_fraction == 0.0;
                        }),
        "spectral compare: best-offset per-band evidence uses the supported interior");

  shifted.offset_series = SpectralOffsetSeries::Reference;
  const auto reference_sweep =
      compare_spectral_groups(centred, displaced, shifted);
  check(reference_sweep.offset_convention.find("reference_nominal") == 0 &&
            std::abs(reference_sweep.best_wavelength_offset_nm -
                     1.0) < 1e-12,
        "spectral compare: reference-axis sensitivity has a named sign convention");

  auto zero_to_positive = shifted;
  zero_to_positive.offset_min_nm = 0.0;
  zero_to_positive.offset_max_nm = 1.0;
  zero_to_positive.offset_step_nm = 0.5;
  const auto zero_to_positive_sweep =
      compare_spectral_groups(centred, displaced, zero_to_positive);
  auto positive_only = shifted;
  positive_only.offset_min_nm = 0.5;
  positive_only.offset_max_nm = 1.0;
  positive_only.offset_step_nm = 0.5;
  const auto positive_only_sweep =
      compare_spectral_groups(centred, displaced, positive_only);
  check(positive_only_sweep.zero_offset_objective.has_value() &&
            std::none_of(positive_only_sweep.offset_sensitivity.begin(),
                         positive_only_sweep.offset_sensitivity.end(),
                         [](const auto& item) {
                           return item.wavelength_offset_nm == 0.0;
                         }),
        "spectral compare: zero baseline is available outside the requested sweep range");
  check_near(
      positive_only_sweep.zero_offset_objective->directional_relative_l2,
      zero_to_positive_sweep.zero_offset_objective->directional_relative_l2,
      0.0,
      "spectral compare: outside-range zero baseline uses the same fixed support");

  zero_to_positive.offset_series = SpectralOffsetSeries::Candidate;
  positive_only.offset_series = SpectralOffsetSeries::Candidate;
  const auto candidate_zero_to_positive =
      compare_spectral_groups(centred, displaced, zero_to_positive);
  const auto candidate_positive_only =
      compare_spectral_groups(centred, displaced, positive_only);
  check_near(
      candidate_positive_only.zero_offset_objective->directional_relative_l2,
      candidate_zero_to_positive.zero_offset_objective->directional_relative_l2,
      0.0,
      "spectral compare: candidate-axis outside-range baseline uses the fixed support");

  auto one_sweep_sample = shifted;
  one_sweep_sample.offset_min_nm = -2.0;
  one_sweep_sample.offset_max_nm = 2.0;
  one_sweep_sample.offset_step_nm = 1.0;
  check(throws_with(
            [&] {
              (void)compare_spectral_groups(centred, displaced,
                                             one_sweep_sample);
            },
            "at least two common supported samples"),
        "spectral compare: a one-sample offset interior is refused");

  shifted.offset_min_nm = -1.0;
  shifted.offset_max_nm = 1.0;
  shifted.offset_step_nm = 0.025;
  const auto decimal_sweep =
      compare_spectral_groups(centred, displaced, shifted);
  check(decimal_sweep.offset_sensitivity.size() == 81 &&
            decimal_sweep.offset_sensitivity[40].wavelength_offset_nm == 0.0 &&
            decimal_sweep.offset_sensitivity.back().wavelength_offset_nm == 1.0,
        "spectral compare: decimal sweep coordinates do not accumulate drift");

  auto singleton = options;
  singleton.common_wavelength_nm = {1.0};
  check(throws_with(
            [&] {
              (void)compare_spectral_groups(reference, candidate, singleton);
            },
            "at least two samples"),
        "spectral compare: a singleton common grid cannot erase shape disagreement");

  check(throws_with(
            [&] {
              auto invalid = options;
              invalid.common_wavelength_nm = {-1.0, 0.0};
              (void)compare_spectral_groups(reference, candidate, invalid);
            },
            "outside the retained range"),
        "spectral compare: extrapolation is refused");

  SpectralComparisonOptions huge_sweep;
  huge_sweep.common_wavelength_nm = {-1.0, 0.0, 1.0};
  huge_sweep.offset_min_nm = 0.0;
  huge_sweep.offset_max_nm = 1e308;
  huge_sweep.offset_step_nm = 1.0;
  const std::vector<SampledSpectrum> huge_axis = {
      {{-1e308, 0.0, 1e308}, {1e-308, 2e-308, 1e-308}}};
  check(throws_with(
            [&] {
              (void)compare_spectral_groups(huge_axis, huge_axis, huge_sweep);
            },
            "finite, bounded"),
        "spectral compare: extreme finite sweep count is refused before integer conversion");

  SpectralComparisonOptions large_finite_options;
  large_finite_options.common_wavelength_nm = {0.0, 1.0, 2.0};
  const std::vector<SampledSpectrum> large_finite_reference = {
      {{0.0, 1.0, 2.0}, {1e308, 1.0, -1e308}}};
  const std::vector<SampledSpectrum> large_finite_candidate = {
      {{0.0, 1.0, 2.0}, {5e307, 1.0, -5e307}}};
  const auto large_finite = compare_spectral_groups(
      large_finite_reference, large_finite_candidate, large_finite_options);
  double contribution_sum = 0.0;
  bool contributions_finite = true;
  for (const auto& band : large_finite.bands) {
    contribution_sum += band.squared_residual_fraction;
    contributions_finite =
        contributions_finite && std::isfinite(band.squared_residual_fraction);
  }
  check(std::isfinite(large_finite.directional_relative_l2) &&
            contributions_finite,
        "spectral compare: finite inputs retain representable comparison evidence");
  check_near(contribution_sum, 1.0, 1e-12,
             "spectral compare: large finite residual contributions remain normalized");

  SpectralComparisonOptions large_interpolation_options;
  large_interpolation_options.common_wavelength_nm = {0.5, 1.0, 1.5};
  const std::vector<SampledSpectrum> large_interpolation = {
      {{0.0, 1.0, 2.0}, {-1e308, 1e308, 1.0}}};
  const auto large_interpolated = compare_spectral_groups(
      large_interpolation, large_interpolation, large_interpolation_options);
  check_near(large_interpolated.directional_relative_l2, 0.0, 0.0,
             "spectral compare: finite convex interpolation avoids intermediate overflow");
}
