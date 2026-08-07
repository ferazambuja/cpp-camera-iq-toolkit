#include "camera_iq/spectral_series.hpp"

#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace camera_iq {
namespace {

std::vector<std::string> csv_row(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t index = 0; index < line.size(); ++index) {
    const char c = line[index];
    if (quoted) {
      if (c == '"') {
        if (index + 1 < line.size() && line[index + 1] == '"') {
          field += '"';
          ++index;
        } else {
          quoted = false;
        }
      } else {
        field += c;
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      fields.push_back(field);
      field.clear();
    } else {
      field += c;
    }
  }
  if (quoted) {
    throw std::runtime_error("spectral series CSV: unterminated quote");
  }
  fields.push_back(field);
  return fields;
}

double finite_number(const std::string& text, std::string_view field) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (...) {
    throw std::runtime_error("spectral series CSV: " + std::string(field) +
                             " must be a finite number");
  }
}

}  // namespace

std::vector<SpectralSeries> read_spectral_series_csv(
    std::string_view csv_text) {
  std::istringstream input{std::string(csv_text)};
  std::string line;
  if (!std::getline(input, line)) {
    throw std::runtime_error("spectral series CSV: unexpected header");
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  if (line != "series_id,reading_id,wavelength_nm,value") {
    throw std::runtime_error("spectral series CSV: unexpected header");
  }

  std::vector<SpectralSeries> result;
  std::map<std::string, std::size_t> series_indices;
  std::map<std::pair<std::string, std::string>, std::size_t> reading_indices;
  std::size_t row_number = 1;
  while (std::getline(input, line)) {
    ++row_number;
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      throw std::runtime_error("spectral series CSV: blank row " +
                               std::to_string(row_number));
    }
    const auto fields = csv_row(line);
    if (fields.size() != 4) {
      throw std::runtime_error("spectral series CSV: row " +
                               std::to_string(row_number) +
                               " must have four fields");
    }
    if (fields[0].empty() || fields[1].empty()) {
      throw std::runtime_error(
          "spectral series CSV: identifiers must not be empty");
    }
    const double wavelength = finite_number(fields[2], "wavelength_nm");
    const double value = finite_number(fields[3], "value");

    auto found_series = series_indices.find(fields[0]);
    if (found_series == series_indices.end()) {
      const std::size_t index = result.size();
      series_indices.emplace(fields[0], index);
      result.push_back(SpectralSeries{fields[0], {}, {}});
      found_series = series_indices.find(fields[0]);
    }
    SpectralSeries& series = result[found_series->second];
    const auto key = std::make_pair(fields[0], fields[1]);
    auto found_reading = reading_indices.find(key);
    if (found_reading == reading_indices.end()) {
      const std::size_t index = series.readings.size();
      reading_indices.emplace(key, index);
      series.reading_ids.push_back(fields[1]);
      series.readings.push_back({{}, {}});
      found_reading = reading_indices.find(key);
    }
    auto& reading = series.readings[found_reading->second];
    if (!reading.wavelength_nm.empty() &&
        wavelength <= reading.wavelength_nm.back()) {
      const std::string reason =
          wavelength == reading.wavelength_nm.back()
              ? "duplicate wavelength"
              : "wavelength axis must be strictly increasing";
      throw std::runtime_error("spectral series CSV: " + reason);
    }
    reading.wavelength_nm.push_back(wavelength);
    reading.values.push_back(value);
  }
  if (result.empty()) {
    throw std::runtime_error("spectral series CSV: no samples");
  }
  for (const auto& series : result) {
    (void)analyze_sampled_spectrum_group(series.readings);
  }
  return result;
}

std::vector<SpectralSeries> read_spectral_series_csv_file(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("spectral series CSV: cannot open input");
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input && !input.eof()) {
    throw std::runtime_error("spectral series CSV: cannot read input");
  }
  return read_spectral_series_csv(contents.str());
}

}  // namespace camera_iq
