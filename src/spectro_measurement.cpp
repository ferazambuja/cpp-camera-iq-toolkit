#include "camera_iq/spectro_measurement.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace camera_iq {
namespace {

const MatArray& required(const MatStruct& fields, std::string_view name) {
  const auto it = fields.find(std::string(name));
  if (it == fields.end()) {
    throw std::runtime_error("spectro measurement: missing field " +
                             std::string(name));
  }
  return it->second;
}

bool has_vector_shape(const MatArray& array) {
  if (array.dims.size() != 2 ||
      (array.dims[0] != 1 && array.dims[1] != 1)) {
    return false;
  }
  const std::size_t length = std::max(array.dims[0], array.dims[1]);
  return length == array.values.size();
}

const std::vector<double>& numeric_vector(const MatStruct& fields,
                                          std::string_view name) {
  const MatArray& array = required(fields, name);
  if (array.logical || !has_vector_shape(array)) {
    throw std::runtime_error("spectro measurement: field " +
                             std::string(name) +
                             " must be a numeric row or column vector");
  }
  if (!std::all_of(array.values.begin(), array.values.end(),
                   [](double value) { return std::isfinite(value); })) {
    throw std::runtime_error("spectro measurement: field " +
                             std::string(name) +
                             " contains a non-finite value");
  }
  return array.values;
}

const MatArray& scalar_array(const MatStruct& fields, std::string_view name) {
  const MatArray& array = required(fields, name);
  if (array.dims != std::vector<std::size_t>({1, 1}) ||
      array.values.size() != 1 || !std::isfinite(array.values[0])) {
    throw std::runtime_error("spectro measurement: field " +
                             std::string(name) +
                             " must be a finite scalar");
  }
  return array;
}

double numeric_scalar(const MatStruct& fields, std::string_view name) {
  const MatArray& array = scalar_array(fields, name);
  if (array.logical) {
    throw std::runtime_error("spectro measurement: field " +
                             std::string(name) + " must be numeric");
  }
  return array.values[0];
}

bool logical_scalar(const MatStruct& fields, std::string_view name) {
  const MatArray& array = scalar_array(fields, name);
  if (!array.logical || (array.values[0] != 0.0 && array.values[0] != 1.0)) {
    throw std::runtime_error("spectro measurement: field " +
                             std::string(name) +
                             " must be a MATLAB logical scalar");
  }
  return array.values[0] == 1.0;
}

std::size_t count_scalar(const MatStruct& fields, std::string_view name) {
  const double value = numeric_scalar(fields, name);
  if (value < 0.0 || std::trunc(value) != value ||
      value >= static_cast<double>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("spectro measurement: field " +
                             std::string(name) +
                             " must be a non-negative integer");
  }
  return static_cast<std::size_t>(value);
}

long double mean_at(const std::vector<SpectroMeasurement>& readings,
                    std::size_t index) {
  const double origin = readings.front().spectral_radiance[index];
  long double offset_sum = 0.0L;
  for (const auto& reading : readings) {
    offset_sum +=
        static_cast<long double>(reading.spectral_radiance[index] - origin);
  }
  return static_cast<long double>(origin) +
         offset_sum / static_cast<long double>(readings.size());
}

long double xyz_mean_at(const std::vector<SpectroMeasurement>& readings,
                        std::size_t channel) {
  const double origin = readings.front().recorded_xyz[channel];
  long double offset_sum = 0.0L;
  for (const auto& reading : readings) {
    offset_sum +=
        static_cast<long double>(reading.recorded_xyz[channel] - origin);
  }
  return static_cast<long double>(origin) +
         offset_sum / static_cast<long double>(readings.size());
}

}  // namespace

SpectroMeasurement spectro_measurement_from_mat(const MatStruct& fields) {
  SpectroMeasurement result;
  result.wavelength_nm = numeric_vector(fields, "wl");
  result.spectral_radiance = numeric_vector(fields, "radiance");
  const auto& xyz = numeric_vector(fields, "XYZ");

  if (result.wavelength_nm.size() != result.spectral_radiance.size() ||
      result.wavelength_nm.size() < 2) {
    throw std::runtime_error(
        "spectro measurement: wavelength and radiance vectors must have "
        "the same non-trivial length");
  }
  if (!std::is_sorted(result.wavelength_nm.begin(), result.wavelength_nm.end(),
                      std::less<double>{}) ||
      std::adjacent_find(result.wavelength_nm.begin(), result.wavelength_nm.end(),
                         std::greater_equal<double>{}) !=
          result.wavelength_nm.end()) {
    throw std::runtime_error(
        "spectro measurement: wavelength axis must be strictly increasing");
  }
  if (xyz.size() != result.recorded_xyz.size()) {
    throw std::runtime_error("spectro measurement: XYZ must have three values");
  }
  std::copy(xyz.begin(), xyz.end(), result.recorded_xyz.begin());

  result.recorded_total_radiance = numeric_scalar(fields, "totalRadiance");
  result.recorded_cct_k = numeric_scalar(fields, "CCT");
  result.recorded_duv = numeric_scalar(fields, "Duv");
  result.repeat_on_error = logical_scalar(fields, "repeatOnError");
  result.current_repetitions = count_scalar(fields, "numCurrentRepetitions");
  return result;
}

SpectroRepeatSummary summarize_spectro_repeats(
    const std::vector<SpectroMeasurement>& readings) {
  if (readings.empty()) {
    throw std::runtime_error("spectro repeat summary: group is empty");
  }

  const auto& axis = readings.front().wavelength_nm;
  if (axis.size() < 2 ||
      !std::all_of(axis.begin(), axis.end(),
                   [](double value) { return std::isfinite(value); }) ||
      std::adjacent_find(axis.begin(), axis.end(),
                         std::greater_equal<double>{}) != axis.end()) {
    throw std::runtime_error(
        "spectro repeat summary: wavelength axis must be finite and strictly "
        "increasing");
  }
  for (std::size_t reading_index = 0; reading_index < readings.size();
       ++reading_index) {
    const auto& reading = readings[reading_index];
    if (reading.wavelength_nm != axis) {
      throw std::runtime_error(
          "spectro repeat summary: wavelength axis mismatch at reading " +
          std::to_string(reading_index));
    }
    if (reading.spectral_radiance.size() != axis.size()) {
      throw std::runtime_error(
          "spectro repeat summary: radiance length mismatch at reading " +
          std::to_string(reading_index));
    }
    if (!std::all_of(reading.spectral_radiance.begin(),
                     reading.spectral_radiance.end(),
                     [](double value) { return std::isfinite(value); }) ||
        !std::all_of(reading.recorded_xyz.begin(), reading.recorded_xyz.end(),
                     [](double value) { return std::isfinite(value); })) {
      throw std::runtime_error(
          "spectro repeat summary: non-finite sample at reading " +
          std::to_string(reading_index));
    }
  }

  SpectroRepeatSummary result;
  result.count = readings.size();
  result.wavelength_nm = axis;
  result.mean_spectral_radiance.resize(axis.size());
  std::vector<long double> precise_radiance_means(axis.size());
  for (std::size_t i = 0; i < axis.size(); ++i) {
    precise_radiance_means[i] = mean_at(readings, i);
    result.mean_spectral_radiance[i] =
        static_cast<double>(precise_radiance_means[i]);
  }
  std::array<long double, 3> precise_xyz_means{};
  for (std::size_t channel = 0; channel < result.mean_recorded_xyz.size();
       ++channel) {
    precise_xyz_means[channel] = xyz_mean_at(readings, channel);
    result.mean_recorded_xyz[channel] =
        static_cast<double>(precise_xyz_means[channel]);
  }

  if (readings.size() == 1) return result;

  result.sample_stddev_spectral_radiance.emplace(axis.size(), 0.0);
  std::array<double, 3> xyz_stddev{};
  const long double denominator =
      static_cast<long double>(readings.size() - 1);
  for (std::size_t i = 0; i < axis.size(); ++i) {
    long double squared_sum = 0.0L;
    const double origin = readings.front().spectral_radiance[i];
    long double offset_sum = 0.0L;
    for (const auto& reading : readings) {
      offset_sum +=
          static_cast<long double>(reading.spectral_radiance[i] - origin);
    }
    const long double mean_offset =
        offset_sum / static_cast<long double>(readings.size());
    for (const auto& reading : readings) {
      const long double difference =
          static_cast<long double>(reading.spectral_radiance[i] - origin) -
          mean_offset;
      squared_sum += difference * difference;
    }
    result.sample_stddev_spectral_radiance->at(i) =
        std::sqrt(static_cast<double>(squared_sum / denominator));
  }
  for (std::size_t channel = 0; channel < xyz_stddev.size(); ++channel) {
    long double squared_sum = 0.0L;
    const double origin = readings.front().recorded_xyz[channel];
    long double offset_sum = 0.0L;
    for (const auto& reading : readings) {
      offset_sum +=
          static_cast<long double>(reading.recorded_xyz[channel] - origin);
    }
    const long double mean_offset =
        offset_sum / static_cast<long double>(readings.size());
    for (const auto& reading : readings) {
      const long double difference =
          static_cast<long double>(reading.recorded_xyz[channel] - origin) -
          mean_offset;
      squared_sum += difference * difference;
    }
    xyz_stddev[channel] =
        std::sqrt(static_cast<double>(squared_sum / denominator));
  }
  result.sample_stddev_recorded_xyz = xyz_stddev;
  return result;
}

}  // namespace camera_iq
