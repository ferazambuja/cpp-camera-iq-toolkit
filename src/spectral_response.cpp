#include "camera_iq/spectral_response.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

constexpr std::size_t kExpectedSamples = 48;
constexpr int kFirstWavelengthNm = 360;
constexpr int kLastWavelengthNm = 830;
constexpr int kWavelengthStepNm = 10;
constexpr std::string_view kLegacyNormalization =
    "legacy_peak_channel_normalized_green_1_no_rescale";
constexpr std::string_view kLegacyValidationTier = "legacy_fidelity_only";

std::string trim_cr(std::string s) {
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

std::vector<std::string> parse_csv_line(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          quoted = false;
        }
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      fields.push_back(field);
      field.clear();
    } else {
      field += c;
    }
  }
  if (quoted) {
    throw std::runtime_error("spectral response CSV: unterminated quote");
  }
  fields.push_back(field);
  return fields;
}

double parse_double_field(const std::string& field,
                          const std::string& context) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(field, &consumed);
    if (consumed != field.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (...) {
    throw std::runtime_error("spectral response CSV: invalid number in " +
                             context + ": '" + field + "'");
  }
}

int rounded_10nm_axis(double wavelength_nm, const std::string& context) {
  if (!std::isfinite(wavelength_nm)) {
    throw std::runtime_error("spectral response CSV: non-finite wavelength in " +
                             context);
  }
  return static_cast<int>(std::llround(wavelength_nm / 10.0) * 10);
}

int expected_axis_at(std::size_t index) {
  return kFirstWavelengthNm + static_cast<int>(index) * kWavelengthStepNm;
}

void validate_axis_sample(std::size_t index, int rounded_nm,
                          const std::string& context) {
  const int expected = expected_axis_at(index);
  if (rounded_nm != expected) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " wavelength axis mismatch at row " +
                             std::to_string(index + 1) + " (expected " +
                             std::to_string(expected) + " nm after rounding)");
  }
}

void validate_axis_vector(const std::vector<int>& axis,
                          const std::string& context) {
  if (axis.size() != kExpectedSamples) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " expected 48 samples, got " +
                             std::to_string(axis.size()));
  }
  for (std::size_t i = 0; i < axis.size(); ++i) {
    validate_axis_sample(i, axis[i], context);
    if (i > 0 && axis[i] - axis[i - 1] != kWavelengthStepNm) {
      throw std::runtime_error("spectral response CSV: " + context +
                               " wavelength axis is not strictly 10 nm");
    }
  }
  if (axis.front() != kFirstWavelengthNm || axis.back() != kLastWavelengthNm) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " wavelength endpoints are not 360/830 nm");
  }
}

void require_width(const std::vector<std::string>& fields, std::size_t width,
                   const std::string& context) {
  if (fields.size() != width) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " expected " + std::to_string(width) +
                             " fields, got " +
                             std::to_string(fields.size()));
  }
}

void write_double_array(JsonWriter& w, const std::vector<double>& values) {
  w.begin_array();
  for (const double value : values) w.value(value);
  w.end_array();
}

void write_int_array(JsonWriter& w, const std::vector<int>& values) {
  w.begin_array();
  for (const int value : values) w.value(value);
  w.end_array();
}

}  // namespace

SpectralResponse normalize_spectral_response(SpectralResponse response) {
  if (response.axis_nm.size() != kExpectedSamples ||
      response.response_r.size() != kExpectedSamples ||
      response.response_g.size() != kExpectedSamples ||
      response.response_b.size() != kExpectedSamples ||
      response.line_spd.size() != kExpectedSamples) {
    throw std::runtime_error(
        "spectral response CSV: cannot normalize incomplete response");
  }

  double max_r = -std::numeric_limits<double>::infinity();
  double max_g = -std::numeric_limits<double>::infinity();
  double max_b = -std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < kExpectedSamples; ++i) {
    max_r = std::max(max_r, response.response_r[i]);
    max_g = std::max(max_g, response.response_g[i]);
    max_b = std::max(max_b, response.response_b[i]);
  }

  constexpr double tol = 1e-9;
  const double global_max = std::max({max_r, max_g, max_b});
  if (std::abs(max_g - 1.0) > tol || std::abs(global_max - 1.0) > tol) {
    throw std::runtime_error(
        "spectral response CSV: legacy response is not green-peak normalized "
        "to 1.0");
  }

  response.normalization = std::string(kLegacyNormalization);
  response.validation_tier = std::string(kLegacyValidationTier);
  return response;
}

SpectralResponse parse_spectral_response(
    const std::filesystem::path& legacy_response_csv,
    const std::filesystem::path& line_spd_csv,
    SpectralResponseProvenance provenance) {
  SpectralResponse response;
  response.camera_model = provenance.camera_model;
  response.dataset_id = provenance.dataset_id;
  response.archive_subset = provenance.archive_subset;

  std::ifstream legacy(legacy_response_csv, std::ios::binary);
  if (!legacy) {
    throw std::runtime_error("spectral response CSV: cannot open " +
                             legacy_response_csv.string());
  }
  std::string line;
  if (!std::getline(legacy, line)) {
    throw std::runtime_error("spectral response CSV: empty legacy response");
  }
  const auto header = parse_csv_line(trim_cr(line));
  require_width(header, 4, "legacy response header");
  if (header[0] != "Wavelength (nm)" || header[1] != "Red" ||
      header[2] != "Green" || header[3] != "Blue") {
    throw std::runtime_error(
        "spectral response CSV: unexpected legacy response header");
  }

  while (std::getline(legacy, line)) {
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    require_width(fields, 4, "legacy response row");
    const std::size_t index = response.axis_nm.size();
    const int rounded_nm = rounded_10nm_axis(
        parse_double_field(fields[0], "legacy response wavelength"),
        "legacy response");
    response.axis_nm.push_back(rounded_nm);
    response.response_r.push_back(
        parse_double_field(fields[1], "legacy response red"));
    response.response_g.push_back(
        parse_double_field(fields[2], "legacy response green"));
    response.response_b.push_back(
        parse_double_field(fields[3], "legacy response blue"));
    validate_axis_sample(index, rounded_nm, "legacy response");
  }
  validate_axis_vector(response.axis_nm, "legacy response");

  std::ifstream spd(line_spd_csv, std::ios::binary);
  if (!spd) {
    throw std::runtime_error("spectral response CSV: cannot open " +
                             line_spd_csv.string());
  }
  if (!std::getline(spd, line)) {
    throw std::runtime_error("spectral response CSV: empty line SPD");
  }
  const auto spd_header = parse_csv_line(trim_cr(line));
  require_width(spd_header, 2, "line SPD header");

  std::vector<int> spd_axis;
  while (std::getline(spd, line)) {
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    require_width(fields, 2, "line SPD row");
    const std::size_t index = spd_axis.size();
    const int rounded_nm = rounded_10nm_axis(
        parse_double_field(fields[0], "line SPD wavelength"), "line SPD");
    validate_axis_sample(index, rounded_nm, "line SPD");
    spd_axis.push_back(rounded_nm);
    const double voltage = parse_double_field(fields[1], "line SPD voltage");
    if (voltage <= 0.0) {
      throw std::runtime_error(
          "spectral response CSV: line SPD voltage must be > 0");
    }
    response.line_spd.push_back(voltage);
  }
  validate_axis_vector(spd_axis, "line SPD");
  if (spd_axis != response.axis_nm) {
    throw std::runtime_error(
        "spectral response CSV: response and line SPD axes differ");
  }

  return normalize_spectral_response(std::move(response));
}

void write_spectral_response_json(std::ostream& os,
                                  const SpectralResponse& response) {
  JsonWriter w(os);
  w.begin_object();
  w.key("camera_model");
  w.value(response.camera_model);
  w.key("dataset_id");
  w.value(response.dataset_id);
  w.key("archive_subset");
  w.value(response.archive_subset);
  w.key("axis_nm");
  write_int_array(w, response.axis_nm);
  w.key("response_rgb");
  w.begin_object();
  w.key("r");
  write_double_array(w, response.response_r);
  w.key("g");
  write_double_array(w, response.response_g);
  w.key("b");
  write_double_array(w, response.response_b);
  w.end_object();
  w.key("line_spd");
  write_double_array(w, response.line_spd);
  w.key("normalization");
  w.value(response.normalization);
  w.key("validation_tier");
  w.value(response.validation_tier);
  w.end_object();
}

}  // namespace camera_iq
