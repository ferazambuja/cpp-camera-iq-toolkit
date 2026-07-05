#include "camera_iq/color_reference.hpp"

#include "camera_iq/json_writer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace camera_iq {
namespace {

std::string trim_cr(std::string s) {
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

std::string trim(std::string_view text) {
  std::size_t first = 0;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first]))) {
    ++first;
  }
  std::size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1]))) {
    --last;
  }
  return std::string(text.substr(first, last - first));
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
    throw std::runtime_error("spectral reference CSV: unterminated quote");
  }
  fields.push_back(field);
  return fields;
}

std::vector<std::string> split_cgats_fields(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c == '"') {
        quoted = false;
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!field.empty()) {
        fields.push_back(field);
        field.clear();
      }
      continue;
    }
    field += c;
  }
  if (quoted) {
    throw std::runtime_error("spectral reference CGATS: unterminated quote");
  }
  if (!field.empty()) fields.push_back(field);
  return fields;
}

double parse_double_field(const std::string& field, const std::string& context,
                          std::string_view source_name) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(field, &consumed);
    if (consumed != field.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (...) {
    throw std::runtime_error(std::string(source_name) +
                             ": invalid number in " + context + ": '" +
                             field + "'");
  }
}

double parse_csv_double_field(const std::string& field,
                              const std::string& context) {
  return parse_double_field(field, context, "spectral reference CSV");
}

std::optional<double> spectral_wavelength_from_field(
    const std::string& field) {
  constexpr std::string_view prefix = "SPECTRAL_NM";
  if (field.rfind(prefix, 0) != 0 || field.size() == prefix.size()) {
    return std::nullopt;
  }
  const std::string suffix = field.substr(prefix.size());
  for (const char c : suffix) {
    if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.') {
      return std::nullopt;
    }
  }
  return parse_double_field(suffix, "data format", "spectral reference CGATS");
}

}  // namespace

SpectralReference read_spectral_reference_csv(const std::filesystem::path& path,
                                              std::string source_label) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("spectral reference CSV: cannot open " +
                             path.string());
  }

  SpectralReference ref;
  ref.source_label = source_label.empty() ? path.generic_string()
                                          : std::move(source_label);

  std::string line;
  if (!std::getline(is, line)) {
    throw std::runtime_error("spectral reference CSV: empty file");
  }
  const auto header = parse_csv_line(trim_cr(line));
  if (header.size() < 2 || header[0] != "patch_id") {
    throw std::runtime_error(
        "spectral reference CSV: header must start with patch_id");
  }

  ref.wavelengths_nm.reserve(header.size() - 1);
  double previous = -std::numeric_limits<double>::infinity();
  for (std::size_t i = 1; i < header.size(); ++i) {
    const double wl = parse_csv_double_field(header[i], "header");
    if (wl <= previous) {
      throw std::runtime_error(
          "spectral reference CSV: wavelengths must be strictly increasing");
    }
    ref.wavelengths_nm.push_back(wl);
    previous = wl;
  }

  std::size_t line_no = 1;
  while (std::getline(is, line)) {
    ++line_no;
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    if (fields.size() != header.size()) {
      throw std::runtime_error("spectral reference CSV: row " +
                               std::to_string(line_no) +
                               " has wrong field count");
    }
    if (fields[0].empty()) {
      throw std::runtime_error("spectral reference CSV: row " +
                               std::to_string(line_no) +
                               " has empty patch_id");
    }

    SpectralReferencePatch patch;
    patch.id = fields[0];
    patch.reflectance.reserve(ref.wavelengths_nm.size());
    for (std::size_t i = 1; i < fields.size(); ++i) {
      patch.reflectance.push_back(
          parse_csv_double_field(fields[i],
                                 "row " + std::to_string(line_no)));
    }
    ref.patches.push_back(std::move(patch));
  }

  if (ref.patches.empty()) {
    throw std::runtime_error("spectral reference CSV: no patch rows");
  }
  return ref;
}

SpectralReference read_spectral_reference_cgats(
    const std::filesystem::path& path, std::string source_label) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("spectral reference CGATS: cannot open " +
                             path.string());
  }

  SpectralReference ref;
  ref.source_label = source_label.empty() ? path.generic_string()
                                          : std::move(source_label);

  std::vector<std::string> lines;
  for (std::string line; std::getline(is, line);) {
    lines.push_back(trim_cr(line));
  }

  std::vector<std::string> fields;
  std::size_t data_format_end = lines.size();
  bool in_format = false;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::string clean = trim(lines[i]);
    if (clean.empty() || clean[0] == '#') continue;
    if (clean == "BEGIN_DATA_FORMAT") {
      in_format = true;
      continue;
    }
    if (clean == "END_DATA_FORMAT") {
      data_format_end = i;
      break;
    }
    if (in_format) {
      const auto tokens = split_cgats_fields(clean);
      fields.insert(fields.end(), tokens.begin(), tokens.end());
    }
  }
  if (fields.empty() || data_format_end == lines.size()) {
    throw std::runtime_error(
        "spectral reference CGATS: missing DATA_FORMAT block");
  }

  std::size_t id_index = fields.size();
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (fields[i] == "SAMPLE_NAME") {
      id_index = i;
      break;
    }
  }
  if (id_index == fields.size()) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
      if (fields[i] == "SAMPLE_ID") {
        id_index = i;
        break;
      }
    }
  }
  if (id_index == fields.size()) {
    throw std::runtime_error(
        "spectral reference CGATS: DATA_FORMAT lacks SAMPLE_NAME/SAMPLE_ID");
  }

  std::vector<std::size_t> spectral_indices;
  double previous = -std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < fields.size(); ++i) {
    const auto wl = spectral_wavelength_from_field(fields[i]);
    if (!wl) continue;
    if (*wl <= previous) {
      throw std::runtime_error(
          "spectral reference CGATS: wavelengths must be strictly increasing");
    }
    spectral_indices.push_back(i);
    ref.wavelengths_nm.push_back(*wl);
    previous = *wl;
  }
  if (spectral_indices.empty()) {
    throw std::runtime_error(
        "spectral reference CGATS: DATA_FORMAT lacks SPECTRAL_NM columns");
  }

  bool in_data = false;
  bool ended_data = false;
  for (std::size_t i = data_format_end + 1; i < lines.size(); ++i) {
    const std::string clean = trim(lines[i]);
    if (clean.empty() || clean[0] == '#') continue;
    if (clean == "BEGIN_DATA") {
      in_data = true;
      continue;
    }
    if (clean == "END_DATA") {
      ended_data = true;
      break;
    }
    if (!in_data) continue;

    const auto values = split_cgats_fields(clean);
    if (values.size() < fields.size()) {
      throw std::runtime_error("spectral reference CGATS: data row " +
                               std::to_string(i + 1) +
                               " has too few fields");
    }
    if (values[id_index].empty()) {
      throw std::runtime_error("spectral reference CGATS: data row " +
                               std::to_string(i + 1) +
                               " has empty sample id");
    }

    SpectralReferencePatch patch;
    patch.id = values[id_index];
    patch.reflectance.reserve(spectral_indices.size());
    for (const std::size_t idx : spectral_indices) {
      patch.reflectance.push_back(
          parse_double_field(values[idx], "row " + std::to_string(i + 1),
                             "spectral reference CGATS"));
    }
    ref.patches.push_back(std::move(patch));
  }

  if (!in_data) {
    throw std::runtime_error("spectral reference CGATS: missing DATA block");
  }
  if (!ended_data) {
    throw std::runtime_error("spectral reference CGATS: missing END_DATA");
  }
  if (ref.patches.empty()) {
    throw std::runtime_error("spectral reference CGATS: no patch rows");
  }
  return ref;
}

SpectralReferenceSummary summarize_spectral_reference(
    const SpectralReference& ref) {
  SpectralReferenceSummary s;
  s.source_label = ref.source_label;
  s.patch_count = ref.patches.size();
  s.band_count = ref.wavelengths_nm.size();
  if (!ref.wavelengths_nm.empty()) {
    s.first_wavelength_nm = ref.wavelengths_nm.front();
    s.last_wavelength_nm = ref.wavelengths_nm.back();
  }
  if (!ref.patches.empty()) {
    s.first_patch_id = ref.patches.front().id;
    s.last_patch_id = ref.patches.back().id;
  }

  double min_v = std::numeric_limits<double>::infinity();
  double max_v = -std::numeric_limits<double>::infinity();
  for (const auto& patch : ref.patches) {
    for (const double v : patch.reflectance) {
      min_v = std::min(min_v, v);
      max_v = std::max(max_v, v);
    }
  }
  if (std::isfinite(min_v)) s.min_reflectance = min_v;
  if (std::isfinite(max_v)) s.max_reflectance = max_v;
  return s;
}

void write_spectral_reference_summary_json(std::ostream& os,
                                           const SpectralReferenceSummary& s) {
  JsonWriter w(os);
  w.begin_object();
  w.key("source_label");
  w.value(s.source_label);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(s.patch_count));
  w.key("band_count");
  w.value(static_cast<std::int64_t>(s.band_count));
  w.key("first_wavelength_nm");
  w.value(s.first_wavelength_nm);
  w.key("last_wavelength_nm");
  w.value(s.last_wavelength_nm);
  w.key("first_patch_id");
  w.value(s.first_patch_id);
  w.key("last_patch_id");
  w.value(s.last_patch_id);
  w.key("min_reflectance");
  w.value(s.min_reflectance);
  w.key("max_reflectance");
  w.value(s.max_reflectance);
  w.end_object();
}

}  // namespace camera_iq
