#include "camera_iq/spectro_analysis.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace camera_iq {
namespace {

class CompensatedSum {
public:
  void add(double value) {
    const double next = sum_ + value;
    if (std::fabs(sum_) >= std::fabs(value)) {
      correction_ += (sum_ - next) + value;
    } else {
      correction_ += (value - next) + sum_;
    }
    sum_ = next;
  }

  double value() const { return sum_ + correction_; }

private:
  double sum_ = 0.0;
  double correction_ = 0.0;
};

double equal_weight_integral(const SpectroMeasurement &reading,
                             double wavelength_step_nm) {
  double max_magnitude = 0.0;
  double ordinary_sum = 0.0;
  double ordinary_correction = 0.0;
  bool ordinary_representable = true;
  for (const double sample : reading.spectral_radiance) {
    if (!std::isfinite(sample)) {
      throw std::runtime_error(
          "spectro analysis: spectral radiance must be finite");
    }
    max_magnitude = std::max(max_magnitude, std::fabs(sample));
    if (ordinary_representable) {
      const double next = ordinary_sum + sample;
      if (!std::isfinite(next)) {
        ordinary_representable = false;
      } else {
        if (std::fabs(ordinary_sum) >= std::fabs(sample)) {
          ordinary_correction += (ordinary_sum - next) + sample;
        } else {
          ordinary_correction += (sample - next) + ordinary_sum;
        }
        ordinary_sum = next;
        ordinary_representable = std::isfinite(ordinary_correction);
      }
    }
  }

  if (ordinary_representable) {
    const double integral =
        (ordinary_sum + ordinary_correction) * wavelength_step_nm;
    if (std::isfinite(integral)) {
      if (integral <= 0.0) {
        throw std::runtime_error(
            "spectro analysis: spectral integral must be finite and positive");
      }
      return integral;
    }
  }

  int sample_exponent = 0;
  if (max_magnitude > 0.0)
    (void)std::frexp(max_magnitude, &sample_exponent);

  CompensatedSum sum;
  for (const double sample : reading.spectral_radiance)
    sum.add(std::ldexp(sample, -sample_exponent));

  int step_exponent = 0;
  const double step_fraction =
      std::frexp(wavelength_step_nm, &step_exponent);
  const double integral = std::ldexp(sum.value() * step_fraction,
                                     sample_exponent + step_exponent);
  if (!std::isfinite(integral) || integral <= 0.0) {
    throw std::runtime_error(
        "spectro analysis: spectral integral must be finite and positive");
  }
  return integral;
}

SpectroChromaticity chromaticity(const std::array<double, 3> &xyz) {
  double max_magnitude = 0.0;
  for (const double value : xyz) {
    if (!std::isfinite(value)) {
      throw std::runtime_error(
          "spectro analysis: recorded XYZ cannot form chromaticity");
    }
    max_magnitude = std::max(max_magnitude, std::fabs(value));
  }
  int exponent = 0;
  if (max_magnitude > 0.0)
    (void)std::frexp(max_magnitude, &exponent);
  const std::array<double, 3> scaled = {
      std::ldexp(xyz[0], -exponent), std::ldexp(xyz[1], -exponent),
      std::ldexp(xyz[2], -exponent)};
  const double xyz_sum = scaled[0] + scaled[1] + scaled[2];
  const double uv_denominator =
      scaled[0] + 15.0 * scaled[1] + 3.0 * scaled[2];
  if (!std::isfinite(xyz_sum) || !std::isfinite(uv_denominator) ||
      xyz_sum <= 0.0 || uv_denominator <= 0.0) {
    throw std::runtime_error(
        "spectro analysis: recorded XYZ cannot form chromaticity");
  }
  return SpectroChromaticity{
      scaled[0] / xyz_sum, scaled[1] / xyz_sum,
      4.0 * scaled[0] / uv_denominator,
      9.0 * scaled[1] / uv_denominator};
}

double sample_stddev(const std::vector<double> &values, double mean) {
  double max_magnitude = std::fabs(mean);
  for (const double value : values) {
    max_magnitude = std::max(max_magnitude, std::fabs(value));
  }
  int exponent = 0;
  if (max_magnitude > 0.0)
    (void)std::frexp(max_magnitude, &exponent);

  const double scaled_mean = std::ldexp(mean, -exponent);
  const double divisor = static_cast<double>(values.size() - 1);
  double scaled_variance = 0.0;
  for (const double value : values) {
    const double difference = std::ldexp(value, -exponent) - scaled_mean;
    scaled_variance += (difference * difference) / divisor;
  }
  const double result = std::ldexp(std::sqrt(scaled_variance), exponent);
  if (!std::isfinite(result)) {
    throw std::runtime_error(
        "spectro analysis: sample standard deviation is not representable");
  }
  return result;
}

double representable_mean(const std::vector<double> &values) {
  double max_magnitude = 0.0;
  for (const double value : values)
    max_magnitude = std::max(max_magnitude, std::fabs(value));
  int exponent = 0;
  if (max_magnitude > 0.0)
    (void)std::frexp(max_magnitude, &exponent);

  CompensatedSum sum;
  const double divisor = static_cast<double>(values.size());
  for (const double value : values) {
    const double term = std::ldexp(value, -exponent) / divisor;
    sum.add(term);
  }
  const double result = std::ldexp(sum.value(), exponent);
  if (!std::isfinite(result))
    throw std::runtime_error("spectro analysis: mean is not representable");
  return result;
}

} // namespace

SpectroGroupAnalysis
analyze_spectro_group(const std::vector<SpectroMeasurement> &readings) {
  if (readings.empty()) {
    throw std::runtime_error("spectro analysis: group is empty");
  }
  const std::vector<double> &wavelengths = readings.front().wavelength_nm;
  if (wavelengths.size() < 2) {
    throw std::runtime_error("spectro analysis: wavelength grid is too short");
  }
  const double step = wavelengths[1] - wavelengths[0];
  if (!std::isfinite(step) || step <= 0.0) {
    throw std::runtime_error(
        "spectro analysis: wavelength step must be finite and positive");
  }
  for (std::size_t index = 1; index < wavelengths.size(); ++index) {
    const double observed_step = wavelengths[index] - wavelengths[index - 1];
    if (std::fabs(observed_step - step) > 1e-9) {
      throw std::runtime_error(
          "spectro analysis: equal-weight integration requires a uniform grid");
    }
  }

  SpectroGroupAnalysis result;
  result.count = readings.size();
  result.wavelength_step_nm = step;
  result.mean_normalized_spectrum.assign(wavelengths.size(), 0.0);
  result.readings.reserve(readings.size());
  std::vector<double> integrals;
  integrals.reserve(readings.size());

  for (const SpectroMeasurement &reading : readings) {
    if (reading.wavelength_nm != wavelengths ||
        reading.spectral_radiance.size() != wavelengths.size()) {
      throw std::runtime_error(
          "spectro analysis: readings must share one wavelength grid");
    }
    SpectroReadingAnalysis analyzed;
    analyzed.spectral_integral = equal_weight_integral(reading, step);
    analyzed.normalized_spectrum.reserve(wavelengths.size());
    for (std::size_t index = 0; index < wavelengths.size(); ++index) {
      const double normalized =
          reading.spectral_radiance[index] / analyzed.spectral_integral;
      if (!std::isfinite(normalized)) {
        throw std::runtime_error(
            "spectro analysis: normalized spectrum is not representable");
      }
      analyzed.normalized_spectrum.push_back(normalized);
    }
    analyzed.recorded_xyz_chromaticity = chromaticity(reading.recorded_xyz);
    integrals.push_back(analyzed.spectral_integral);
    result.readings.push_back(std::move(analyzed));
  }
  result.mean_spectral_integral = representable_mean(integrals);
  for (std::size_t wavelength = 0; wavelength < wavelengths.size();
       ++wavelength) {
    std::vector<double> values;
    values.reserve(readings.size());
    for (const auto &reading : result.readings)
      values.push_back(reading.normalized_spectrum[wavelength]);
    result.mean_normalized_spectrum[wavelength] = representable_mean(values);
  }

  if (readings.size() == 1)
    return result;

  result.sample_stddev_spectral_integral =
      sample_stddev(integrals, result.mean_spectral_integral);
  result.coefficient_of_variation =
      *result.sample_stddev_spectral_integral / result.mean_spectral_integral;
  if (!std::isfinite(*result.coefficient_of_variation)) {
    throw std::runtime_error(
        "spectro analysis: coefficient of variation is not representable");
  }

  std::vector<double> normalized_stddev(wavelengths.size(), 0.0);
  for (std::size_t wavelength = 0; wavelength < wavelengths.size();
       ++wavelength) {
    std::vector<double> values;
    values.reserve(readings.size());
    for (const auto &reading : result.readings) {
      values.push_back(reading.normalized_spectrum[wavelength]);
    }
    normalized_stddev[wavelength] =
        sample_stddev(values, result.mean_normalized_spectrum[wavelength]);
  }
  result.sample_stddev_normalized_spectrum = std::move(normalized_stddev);

  double mean_norm = 0.0;
  for (const double sample : result.mean_normalized_spectrum) {
    mean_norm = std::hypot(mean_norm, sample);
  }
  if (!std::isfinite(mean_norm) || mean_norm <= 0.0) {
    throw std::runtime_error(
        "spectro analysis: normalized mean has no representable L2 norm");
  }
  double max_shape_relative_l2 = 0.0;
  for (const auto &reading : result.readings) {
    double residual_norm = 0.0;
    for (std::size_t index = 0; index < wavelengths.size(); ++index) {
      const double residual = reading.normalized_spectrum[index] -
                              result.mean_normalized_spectrum[index];
      residual_norm = std::hypot(residual_norm, residual);
    }
    const double relative_l2 = residual_norm / mean_norm;
    if (!std::isfinite(relative_l2)) {
      throw std::runtime_error(
          "spectro analysis: relative L2 residual is not representable");
    }
    max_shape_relative_l2 = std::max(max_shape_relative_l2, relative_l2);
  }
  result.max_shape_relative_l2 = max_shape_relative_l2;

  double max_pair_delta_u_prime_v_prime = 0.0;
  for (std::size_t a = 0; a < result.readings.size(); ++a) {
    for (std::size_t b = a + 1; b < result.readings.size(); ++b) {
      const auto &ca = result.readings[a].recorded_xyz_chromaticity;
      const auto &cb = result.readings[b].recorded_xyz_chromaticity;
      max_pair_delta_u_prime_v_prime = std::max(
          max_pair_delta_u_prime_v_prime,
          std::hypot(ca.u_prime - cb.u_prime, ca.v_prime - cb.v_prime));
    }
  }
  result.max_pair_delta_u_prime_v_prime =
      max_pair_delta_u_prime_v_prime;
  return result;
}

} // namespace camera_iq
