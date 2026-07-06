#include "camera_iq/color_reference.hpp"

#include "camera_iq/json_writer.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
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

double parse_camera_double_field(const std::string& field,
                                 const std::string& context) {
  return parse_double_field(field, context, "camera RGB CSV");
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

std::string header_value(const std::map<std::string, std::string>& headers,
                         const std::string& key) {
  const auto it = headers.find(key);
  if (it == headers.end()) return {};
  return it->second;
}

bool near_equal(double a, double b) {
  return std::abs(a - b) <= 1e-6;
}

double pearson_correlation(const std::vector<double>& a,
                           const std::vector<double>& b,
                           const std::string& label) {
  if (a.size() != b.size() || a.size() < 2) {
    throw std::runtime_error("reference pairing: " + label +
                             " requires matching vectors with at least 2 rows");
  }
  double mean_a = 0;
  double mean_b = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    mean_a += a[i];
    mean_b += b[i];
  }
  mean_a /= static_cast<double>(a.size());
  mean_b /= static_cast<double>(b.size());

  double cov = 0;
  double var_a = 0;
  double var_b = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double da = a[i] - mean_a;
    const double db = b[i] - mean_b;
    cov += da * db;
    var_a += da * da;
    var_b += db * db;
  }
  if (var_a <= 0 || var_b <= 0) {
    throw std::runtime_error("reference pairing: " + label +
                             " has zero variance");
  }
  return cov / std::sqrt(var_a * var_b);
}

double band_mean(const SpectralReferencePatch& patch,
                 const std::vector<double>& wavelengths_nm, double lo,
                 double hi) {
  double sum = 0;
  std::size_t count = 0;
  for (std::size_t i = 0; i < wavelengths_nm.size(); ++i) {
    const double wl = wavelengths_nm[i];
    if (wl < lo || wl > hi) continue;
    sum += patch.reflectance[i];
    ++count;
  }
  if (count == 0) {
    throw std::runtime_error(
        "reference pairing: reference lacks required spectral band");
  }
  return sum / static_cast<double>(count);
}

double normalized_difference(double a, double b) {
  constexpr double eps = 1e-12;
  return (a - b) / (std::abs(a) + std::abs(b) + eps);
}

double pairing_score(const SpectralReferencePairing& pairing) {
  return std::min({pairing.luminance_correlation,
                   pairing.red_green_correlation,
                   pairing.blue_green_correlation});
}

std::vector<CameraRgbPatch> reoriented_sg_camera_rgb(
    const std::vector<CameraRgbPatch>& camera_rgb, std::string_view orientation) {
  constexpr int rows = 10;
  constexpr int columns = 14;
  constexpr std::size_t patch_count =
      static_cast<std::size_t>(rows * columns);
  if (camera_rgb.size() != patch_count) {
    throw std::runtime_error(
        "reference orientation: ColorChecker SG controls require 140 patches");
  }

  std::vector<CameraRgbPatch> out;
  out.reserve(camera_rgb.size());
  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < columns; ++col) {
      int source_row = row;
      int source_col = col;
      if (orientation == "column_flip" || orientation == "rotate_180") {
        source_col = columns - 1 - source_col;
      }
      if (orientation == "row_flip" || orientation == "rotate_180") {
        source_row = rows - 1 - source_row;
      }
      out.push_back(
          camera_rgb[static_cast<std::size_t>(source_row * columns +
                                              source_col)]);
    }
  }
  return out;
}

}  // namespace

SpectralReference read_spectral_reference_csv(const std::filesystem::path& path,
                                              std::string source_label,
                                              SpectralReferenceProvenance provenance) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("spectral reference CSV: cannot open " +
                             path.string());
  }

  SpectralReference ref;
  ref.source_label = source_label.empty() ? path.generic_string()
                                          : std::move(source_label);
  ref.provenance = std::move(provenance);

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
    const std::filesystem::path& path, std::string source_label,
    SpectralReferenceProvenance provenance) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("spectral reference CGATS: cannot open " +
                             path.string());
  }

  SpectralReference ref;
  ref.source_label = source_label.empty() ? path.generic_string()
                                          : std::move(source_label);
  ref.provenance = std::move(provenance);

  std::vector<std::string> lines;
  for (std::string line; std::getline(is, line);) {
    lines.push_back(trim_cr(line));
  }

  std::vector<std::string> fields;
  std::map<std::string, std::string> headers;
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
    } else {
      const auto tokens = split_cgats_fields(clean);
      if (tokens.size() >= 2) headers.emplace(tokens[0], tokens[1]);
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
  if (ref.provenance.source.empty()) {
    ref.provenance.source = header_value(headers, "MEASUREMENT_SOURCE");
  }
  if (ref.provenance.illuminant.empty()) {
    ref.provenance.illuminant = header_value(headers, "ILLUMINATION_NAME");
  }
  if (ref.provenance.observer.empty()) {
    ref.provenance.observer = header_value(headers, "OBSERVER_ANGLE");
  }
  if (ref.provenance.unit.empty()) {
    ref.provenance.unit = "spectral_reflectance";
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
  s.provenance = ref.provenance;

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

void validate_spectral_reference(const SpectralReference& ref,
                                 const SpectralReferenceValidation& rule) {
  if (rule.expected_patch_count &&
      ref.patches.size() != *rule.expected_patch_count) {
    throw std::runtime_error("spectral reference validation: expected " +
                             std::to_string(*rule.expected_patch_count) +
                             " patches, got " +
                             std::to_string(ref.patches.size()));
  }
  if (rule.expected_band_count &&
      ref.wavelengths_nm.size() != *rule.expected_band_count) {
    throw std::runtime_error("spectral reference validation: expected " +
                             std::to_string(*rule.expected_band_count) +
                             " bands, got " +
                             std::to_string(ref.wavelengths_nm.size()));
  }
  if (rule.first_wavelength_nm && ref.wavelengths_nm.empty()) {
    throw std::runtime_error(
        "spectral reference validation: missing wavelength axis");
  }
  if (rule.last_wavelength_nm && ref.wavelengths_nm.empty()) {
    throw std::runtime_error(
        "spectral reference validation: missing wavelength axis");
  }
  if (rule.first_wavelength_nm &&
      !near_equal(ref.wavelengths_nm.front(), *rule.first_wavelength_nm)) {
    throw std::runtime_error(
        "spectral reference validation: unexpected first wavelength");
  }
  if (rule.last_wavelength_nm &&
      !near_equal(ref.wavelengths_nm.back(), *rule.last_wavelength_nm)) {
    throw std::runtime_error(
        "spectral reference validation: unexpected last wavelength");
  }

  for (const auto& patch : ref.patches) {
    if (patch.reflectance.size() != ref.wavelengths_nm.size()) {
      throw std::runtime_error(
          "spectral reference validation: patch width mismatch");
    }
    for (const double v : patch.reflectance) {
      if (!std::isfinite(v)) {
        throw std::runtime_error(
            "spectral reference validation: non-finite reflectance");
      }
      if (rule.min_reflectance && v < *rule.min_reflectance) {
        throw std::runtime_error(
            "spectral reference validation: reflectance below minimum");
      }
      if (rule.max_reflectance && v > *rule.max_reflectance) {
        throw std::runtime_error(
            "spectral reference validation: reflectance above maximum");
      }
    }
  }
}

std::vector<CameraRgbPatch> read_camera_rgb_csv(
    const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("camera RGB CSV: cannot open " + path.string());
  }

  std::vector<CameraRgbPatch> out;
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(is, line)) {
    ++line_no;
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    if (fields.size() != 3) {
      throw std::runtime_error("camera RGB CSV: row " +
                               std::to_string(line_no) +
                               " must have 3 fields");
    }
    CameraRgbPatch patch;
    patch.r = parse_camera_double_field(fields[0],
                                        "row " + std::to_string(line_no));
    patch.g = parse_camera_double_field(fields[1],
                                        "row " + std::to_string(line_no));
    patch.b = parse_camera_double_field(fields[2],
                                        "row " + std::to_string(line_no));
    out.push_back(patch);
  }
  if (out.empty()) {
    throw std::runtime_error("camera RGB CSV: no patch rows");
  }
  return out;
}

SpectralReferencePairing evaluate_reference_pairing(
    const SpectralReference& ref, const std::vector<CameraRgbPatch>& camera_rgb,
    SpectralReferencePairingThresholds thresholds) {
  if (ref.patches.size() != camera_rgb.size()) {
    throw std::runtime_error(
        "reference pairing: camera RGB rows and reference patches differ");
  }
  if (ref.patches.size() < 3) {
    throw std::runtime_error(
        "reference pairing: at least 3 patches are required");
  }

  std::vector<double> camera_luminance;
  std::vector<double> camera_rg;
  std::vector<double> camera_bg;
  std::vector<double> ref_luminance;
  std::vector<double> ref_rg;
  std::vector<double> ref_bg;
  camera_luminance.reserve(camera_rgb.size());
  camera_rg.reserve(camera_rgb.size());
  camera_bg.reserve(camera_rgb.size());
  ref_luminance.reserve(camera_rgb.size());
  ref_rg.reserve(camera_rgb.size());
  ref_bg.reserve(camera_rgb.size());

  for (std::size_t i = 0; i < ref.patches.size(); ++i) {
    const auto& patch = ref.patches[i];
    const double ref_r = band_mean(patch, ref.wavelengths_nm, 600.0, 700.0);
    const double ref_g = band_mean(patch, ref.wavelengths_nm, 500.0, 580.0);
    const double ref_b = band_mean(patch, ref.wavelengths_nm, 430.0, 490.0);
    ref_luminance.push_back(ref_g);
    ref_rg.push_back(normalized_difference(ref_r, ref_g));
    ref_bg.push_back(normalized_difference(ref_b, ref_g));

    const auto& rgb = camera_rgb[i];
    camera_luminance.push_back(rgb.g);
    camera_rg.push_back(normalized_difference(rgb.r, rgb.g));
    camera_bg.push_back(normalized_difference(rgb.b, rgb.g));
  }

  SpectralReferencePairing out;
  out.patch_count = ref.patches.size();
  out.thresholds = thresholds;
  out.luminance_correlation =
      pearson_correlation(camera_luminance, ref_luminance, "luminance");
  out.red_green_correlation =
      pearson_correlation(camera_rg, ref_rg, "red-green chroma proxy");
  out.blue_green_correlation =
      pearson_correlation(camera_bg, ref_bg, "blue-green chroma proxy");
  out.passes =
      out.luminance_correlation >= thresholds.min_luminance_correlation &&
      out.red_green_correlation >= thresholds.min_red_green_correlation &&
      out.blue_green_correlation >= thresholds.min_blue_green_correlation;
  return out;
}

SpectralReferenceOrientationReport evaluate_reference_orientation_controls(
    const SpectralReference& ref, const std::vector<CameraRgbPatch>& camera_rgb,
    SpectralReferencePairingThresholds thresholds) {
  constexpr std::size_t kColorCheckerSgPatchCount = 140;
  if (ref.patches.size() != kColorCheckerSgPatchCount ||
      camera_rgb.size() != kColorCheckerSgPatchCount) {
    throw std::runtime_error(
        "reference orientation: ColorChecker SG controls require 140 patches");
  }

  SpectralReferenceOrientationReport report;
  const std::vector<std::string> orientations = {
      "direct", "column_flip", "row_flip", "rotate_180"};
  double best_score = -std::numeric_limits<double>::infinity();
  for (const auto& orientation : orientations) {
    const auto oriented_camera =
        orientation == "direct"
            ? camera_rgb
            : reoriented_sg_camera_rgb(camera_rgb, orientation);
    SpectralReferenceOrientationScore score;
    score.orientation = orientation;
    score.pairing =
        evaluate_reference_pairing(ref, oriented_camera, thresholds);
    score.aggregate_score = pairing_score(score.pairing);
    if (score.aggregate_score > best_score + 1e-12) {
      best_score = score.aggregate_score;
      report.best_orientation = orientation;
    }
    report.scores.push_back(std::move(score));
  }

  report.orientation_valid =
      report.best_orientation == "direct" && !report.scores.empty() &&
      report.scores.front().pairing.passes;
  return report;
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
  w.key("provenance");
  w.begin_object();
  w.key("source");
  w.value(s.provenance.source);
  w.key("illuminant");
  w.value(s.provenance.illuminant);
  w.key("observer");
  w.value(s.provenance.observer);
  w.key("unit");
  w.value(s.provenance.unit);
  w.key("numbering_order");
  w.value(s.provenance.numbering_order);
  w.end_object();
  w.end_object();
}

}  // namespace camera_iq
