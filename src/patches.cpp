#include "camera_iq/patches.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/json_writer.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {
namespace {

constexpr std::string_view kFlatFieldNormalizationPolicy =
    "per_channel_mean_valid_samples";
constexpr double kFlatNearCeilingFraction = 0.98;

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
    throw std::runtime_error("patch CSV: unterminated quote");
  }
  fields.push_back(field);
  return fields;
}

double parse_double(const std::string& field, const std::string& context) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(field, &consumed);
    if (consumed != field.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (...) {
    throw std::runtime_error("patch coords CSV: invalid number in " + context);
  }
}

double rgb_component(const CameraRgbPatch& rgb, int component) {
  if (component == 0) return rgb.r;
  if (component == 1) return rgb.g;
  return rgb.b;
}

double patch_center_x(const PatchCoord& coord) {
  return coord.x - 1.0 + coord.width / 2.0;
}

double patch_center_y(const PatchCoord& coord) {
  return coord.y - 1.0 + coord.height / 2.0;
}

std::string reference_patch_id_from_index(std::size_t index) {
  const int row = static_cast<int>(index / 14);
  const int column = static_cast<int>(index % 14);
  std::string id;
  id.push_back(static_cast<char>('A' + column));
  id += std::to_string(row + 1);
  return id;
}

int reference_patch_row_from_index(std::size_t index) {
  return static_cast<int>(index / 14);
}

int reference_patch_column_from_index(std::size_t index) {
  return static_cast<int>(index % 14);
}

double flat_denominator(double value, double floor_value) {
  if (!std::isfinite(value) || value <= floor_value) return floor_value;
  return value;
}

void write_rgb(JsonWriter& w, const CameraRgbPatch& rgb) {
  w.begin_object();
  w.key("r");
  w.value(rgb.r);
  w.key("g");
  w.value(rgb.g);
  w.key("b");
  w.value(rgb.b);
  w.end_object();
}

void write_coord(JsonWriter& w, const PatchCoord& coord) {
  w.begin_object();
  w.key("x");
  w.value(coord.x);
  w.key("y");
  w.value(coord.y);
  w.key("width");
  w.value(coord.width);
  w.key("height");
  w.value(coord.height);
  w.end_object();
}

void write_point(JsonWriter& w, const PatchGeometryReportPoint& point) {
  w.begin_object();
  w.key("x");
  w.value(point.x);
  w.key("y");
  w.value(point.y);
  w.end_object();
}

void write_generated_geometry(
    JsonWriter& w, const std::optional<PatchGeometryReport>& geometry) {
  if (!geometry) {
    w.null();
    return;
  }
  w.begin_object();
  w.key("chart_model");
  w.value(geometry->chart_model);
  w.key("method");
  w.value(geometry->method);
  w.key("corner_order");
  w.begin_array();
  w.value("top_left");
  w.value("top_right");
  w.value("bottom_right");
  w.value("bottom_left");
  w.end_array();
  w.key("corners");
  w.begin_object();
  w.key("top_left");
  write_point(w, geometry->corners[0]);
  w.key("top_right");
  write_point(w, geometry->corners[1]);
  w.key("bottom_right");
  write_point(w, geometry->corners[2]);
  w.key("bottom_left");
  write_point(w, geometry->corners[3]);
  w.end_object();
  w.key("patches");
  w.begin_array();
  for (const auto& patch : geometry->patches) {
    w.begin_object();
    w.key("reference_patch_id");
    w.value(patch.reference_patch_id);
    w.key("row");
    w.value(patch.row);
    w.key("column");
    w.value(patch.column);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_patch(JsonWriter& w, const PatchMean& patch, std::size_t index,
                 const std::vector<std::string>& sample_names) {
  w.begin_object();
  w.key("index");
  w.value(static_cast<std::int64_t>(index));
  if (index < sample_names.size()) {
    w.key("sample_name");
    w.value(sample_names[index]);
  }
  w.key("source_coord");
  write_coord(w, patch.source_coord);
  w.key("actual_roi");
  w.begin_object();
  w.key("x");
  w.value(patch.x);
  w.key("y");
  w.value(patch.y);
  w.key("width");
  w.value(patch.width);
  w.key("height");
  w.value(patch.height);
  w.end_object();
  w.key("sample_count");
  w.value(static_cast<std::int64_t>(patch.sample_count));
  w.key("rgb_mean");
  write_rgb(w, patch.rgb);
  w.end_object();
}

void write_corrections(JsonWriter& w, std::string_view flat_label,
                       const std::optional<FlatFieldCorrectionSummary>& flat,
                       const std::optional<WhiteBalanceGains>& wb,
                       std::string_view wb_policy) {
  w.begin_object();
  w.key("flat_field");
  if (flat) {
    w.begin_object();
    w.key("path");
    w.value(flat_label);
    w.key("source");
    w.value("bilinear_demosaic_black_subtracted_raw");
    w.key("normalization");
    w.value(kFlatFieldNormalizationPolicy);
    w.key("normalizer");
    write_rgb(w, flat->normalizer);
    w.key("floor_value");
    w.value(flat->floor_value);
    w.key("pixel_count");
    w.value(static_cast<std::int64_t>(flat->pixel_count));
    w.key("valid_sample_count");
    w.value(static_cast<std::int64_t>(flat->valid_sample_count));
    w.key("clamped_sample_count");
    w.value(static_cast<std::int64_t>(flat->clamped_sample_count));
    w.key("near_ceiling_sample_count");
    w.value(static_cast<std::int64_t>(flat->near_ceiling_sample_count));
    w.key("near_ceiling_fraction");
    w.value(flat->near_ceiling_fraction);
    w.key("near_ceiling_threshold_fraction");
    w.value(kFlatNearCeilingFraction);
    w.key("max_allowed_near_ceiling_fraction");
    w.value(flat->max_allowed_near_ceiling_fraction);
    w.end_object();
  } else {
    w.null();
  }

  w.key("white_balance");
  if (wb) {
    w.begin_object();
    w.key("policy");
    w.value(wb_policy);
    w.key("gains");
    write_rgb(w, {wb->r, wb->g, wb->b});
    w.end_object();
  } else {
    w.begin_object();
    w.key("policy");
    w.value("none");
    w.end_object();
  }
  w.end_object();
}

void write_comparison(JsonWriter& w, const PatchComparison& comparison,
                      std::string_view reference_label) {
  w.begin_object();
  w.key("reference_rgb_path");
  w.value(reference_label);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(comparison.patch_count));
  w.key("method");
  w.value("per_channel_pearson_direct_error_and_affine_fit");
  w.key("channels");
  w.begin_array();
  for (const auto& c : comparison.channels) {
    w.begin_object();
    w.key("channel");
    w.value(c.channel);
    w.key("correlation");
    w.value(c.correlation);
    w.key("slope");
    w.value(c.slope);
    w.key("intercept");
    w.value(c.intercept);
    w.key("mean_error_before_affine");
    w.value(c.mean_error_before_affine);
    w.key("rmse_before_affine");
    w.value(c.rmse_before_affine);
    w.key("max_abs_error_before_affine");
    w.value(c.max_abs_error_before_affine);
    w.key("rmse_after_affine");
    w.value(c.rmse_after_affine);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_orientation_pairing(JsonWriter& w,
                               const SpectralReferencePairing& pairing) {
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(pairing.patch_count));
  w.key("luminance_correlation");
  w.value(pairing.luminance_correlation);
  w.key("red_green_correlation");
  w.value(pairing.red_green_correlation);
  w.key("blue_green_correlation");
  w.value(pairing.blue_green_correlation);
  w.key("min_luminance_correlation");
  w.value(pairing.thresholds.min_luminance_correlation);
  w.key("min_red_green_correlation");
  w.value(pairing.thresholds.min_red_green_correlation);
  w.key("min_blue_green_correlation");
  w.value(pairing.thresholds.min_blue_green_correlation);
  w.key("passes_thresholds");
  w.value(pairing.passes);
}

void write_orientation_validation(
    JsonWriter& w,
    const std::optional<SpectralReferenceOrientationReport>& orientation) {
  if (!orientation) {
    w.null();
    return;
  }
  w.begin_object();
  w.key("method");
  w.value(orientation->method);
  w.key("orientation_valid");
  w.value(orientation->orientation_valid);
  w.key("best_orientation");
  w.value(orientation->best_orientation);
  w.key("scores");
  w.begin_array();
  for (const auto& score : orientation->scores) {
    w.begin_object();
    w.key("orientation");
    w.value(score.orientation);
    w.key("aggregate_score_min_correlation");
    w.value(score.aggregate_score);
    write_orientation_pairing(w, score.pairing);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_metric_summary(JsonWriter& w,
                          const LocalizationMetricSummary& metrics) {
  w.begin_object();
  w.key("sample_count");
  w.value(static_cast<std::int64_t>(metrics.sample_count));
  w.key("rms_px");
  w.value(metrics.rms_px);
  w.key("max_px");
  w.value(metrics.max_px);
  w.key("dx_dy_anisotropy");
  w.value(metrics.dx_dy_anisotropy);
  w.key("adjacent_vector_cosine");
  w.value(metrics.adjacent_vector_cosine);
  w.key("dx_rms_px");
  w.value(metrics.dx_rms_px);
  w.key("dy_rms_px");
  w.value(metrics.dy_rms_px);
  w.end_object();
}

void write_model_comparison(
    JsonWriter& w,
    const std::optional<LocalizationModelComparison>& comparison) {
  if (!comparison) {
    w.null();
    return;
  }
  w.begin_object();
  w.key("method");
  w.value(comparison->method);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(comparison->patch_count));
  w.key("diagnostic_only");
  w.value(comparison->diagnostic_only);
  w.key("predeclared_gate_revision");
  w.value(comparison->predeclared_gate_revision);
  w.key("radial_affine_baselines_reported");
  w.value(comparison->radial_affine_baselines_reported);
  w.key("observed_anisotropy_dx_over_dy");
  w.value(comparison->observed_anisotropy_dx_over_dy);
  w.key("isotropic_radial_predicted_anisotropy_dx_over_dy");
  w.value(comparison->isotropic_radial_predicted_anisotropy_dx_over_dy);
  w.key("identifiability_note");
  w.value(comparison->identifiability_note);
  w.key("models");
  w.begin_array();
  for (const auto& model : comparison->models) {
    w.begin_object();
    w.key("name");
    w.value(model.name);
    w.key("hypothesis");
    w.value(model.hypothesis);
    w.key("degrees_of_freedom");
    w.value(static_cast<std::int64_t>(model.degrees_of_freedom));
    w.key("production_candidate");
    w.value(model.production_candidate);
    w.key("in_sample");
    write_metric_summary(w, model.in_sample);
    w.key("heldout_scores");
    w.begin_array();
    for (const auto& score : model.heldout_scores) {
      w.begin_object();
      w.key("split");
      w.value(score.split);
      w.key("folds");
      w.value(static_cast<std::int64_t>(score.folds));
      w.key("metrics");
      write_metric_summary(w, score.metrics);
      w.end_object();
    }
    w.end_array();
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_independent_center_check(
    JsonWriter& w,
    const std::optional<LocalizationIndependentCenterCheck>& check) {
  if (!check) {
    w.null();
    return;
  }
  w.begin_object();
  w.key("attempted");
  w.value(check->attempted);
  w.key("method");
  w.value(check->method);
  w.key("valid_count");
  w.value(static_cast<std::int64_t>(check->valid_count));
  w.key("generated_grid_rms_px");
  w.value(check->generated_grid_rms_px);
  w.key("rawdigger_oracle_rms_px");
  w.value(check->rawdigger_oracle_rms_px);
  w.key("tracks");
  w.value(check->tracks);
  w.key("interpretation");
  w.value(check->interpretation);
  w.end_object();
}

void write_localization_validation(
    JsonWriter& w,
    const std::optional<PatchLocalizationValidation>& localization) {
  if (!localization) {
    w.null();
    return;
  }
  w.begin_object();
  w.key("method");
  w.value(localization->method);
  w.key("oracle_path");
  w.value(localization->oracle_label);
  w.key("corner_source");
  w.value(localization->corner_source);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(localization->patch_count));
  w.key("predeclared_gates");
  w.begin_object();
  w.key("expected_patch_count");
  w.value(static_cast<std::int64_t>(
      localization->thresholds.expected_patch_count));
  w.key("max_center_error_px");
  w.value(localization->thresholds.max_center_error_px);
  w.key("min_channel_correlation");
  w.value(localization->thresholds.min_channel_correlation);
  w.key("max_abs_mean_error_dn");
  w.value(localization->thresholds.max_abs_mean_error_dn);
  w.end_object();
  w.key("max_center_error_px");
  w.value(localization->max_center_error_px);
  w.key("rms_center_error_px");
  w.value(localization->rms_center_error_px);
  w.key("patch_count_gate_passes");
  w.value(localization->patch_count_gate_passes);
  w.key("center_gate_passes");
  w.value(localization->center_gate_passes);
  w.key("correlation_gate_passes");
  w.value(localization->correlation_gate_passes);
  w.key("mean_error_gate_passes");
  w.value(localization->mean_error_gate_passes);
  w.key("passes");
  w.value(localization->passes);
  w.key("channels");
  w.begin_array();
  for (const auto& c : localization->rgb_comparison.channels) {
    w.begin_object();
    w.key("channel");
    w.value(c.channel);
    w.key("correlation");
    w.value(c.correlation);
    w.key("mean_error");
    w.value(c.mean_error_before_affine);
    w.key("rmse");
    w.value(c.rmse_before_affine);
    w.key("max_abs_error");
    w.value(c.max_abs_error_before_affine);
    w.key("correlation_gate_passes");
    w.value(c.correlation >=
            localization->thresholds.min_channel_correlation);
    w.key("mean_error_gate_passes");
    w.value(c.max_abs_error_before_affine <=
            localization->thresholds.max_abs_mean_error_dn);
    w.end_object();
  }
  w.end_array();
  w.key("model_comparison");
  write_model_comparison(w, localization->model_comparison);
  w.key("independent_center_check");
  write_independent_center_check(w, localization->independent_center_check);
  w.key("center_residuals");
  w.begin_array();
  for (const auto& residual : localization->center_residuals) {
    w.begin_object();
    w.key("reference_patch_id");
    w.value(residual.reference_patch_id);
    w.key("row");
    w.value(static_cast<std::int64_t>(residual.row));
    w.key("column");
    w.value(static_cast<std::int64_t>(residual.column));
    w.key("generated_center_x");
    w.value(residual.generated_center_x);
    w.key("generated_center_y");
    w.value(residual.generated_center_y);
    w.key("oracle_center_x");
    w.value(residual.oracle_center_x);
    w.key("oracle_center_y");
    w.value(residual.oracle_center_y);
    w.key("dx_px");
    w.value(residual.dx_px);
    w.key("dy_px");
    w.value(residual.dy_px);
    w.key("distance_px");
    w.value(residual.distance_px);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

PatchChannelComparison compare_channel(const std::vector<PatchMean>& patches,
                                       const std::vector<CameraRgbPatch>& ref,
                                       int component) {
  PatchChannelComparison out;
  out.channel = component == 0 ? "R" : (component == 1 ? "G" : "B");

  double mean_x = 0;
  double mean_y = 0;
  for (std::size_t i = 0; i < patches.size(); ++i) {
    mean_x += rgb_component(patches[i].rgb, component);
    mean_y += rgb_component(ref[i], component);
  }
  mean_x /= static_cast<double>(patches.size());
  mean_y /= static_cast<double>(patches.size());

  double cov = 0;
  double var_x = 0;
  double var_y = 0;
  for (std::size_t i = 0; i < patches.size(); ++i) {
    const double dx = rgb_component(patches[i].rgb, component) - mean_x;
    const double dy = rgb_component(ref[i], component) - mean_y;
    cov += dx * dy;
    var_x += dx * dx;
    var_y += dy * dy;
  }
  if (var_x <= 0 || var_y <= 0) {
    throw std::runtime_error("patch comparison: zero-variance channel");
  }

  out.correlation = cov / std::sqrt(var_x * var_y);
  out.slope = cov / var_x;
  out.intercept = mean_y - out.slope * mean_x;

  double error_sum = 0;
  double direct_sumsq = 0;
  double max_abs_error = 0;
  double affine_sumsq = 0;
  for (std::size_t i = 0; i < patches.size(); ++i) {
    const double direct_residual = rgb_component(patches[i].rgb, component) -
                                   rgb_component(ref[i], component);
    error_sum += direct_residual;
    direct_sumsq += direct_residual * direct_residual;
    max_abs_error = std::max(max_abs_error, std::abs(direct_residual));

    const double predicted =
        out.slope * rgb_component(patches[i].rgb, component) + out.intercept;
    const double residual = predicted - rgb_component(ref[i], component);
    affine_sumsq += residual * residual;
  }
  out.mean_error_before_affine =
      error_sum / static_cast<double>(patches.size());
  out.rmse_before_affine =
      std::sqrt(direct_sumsq / static_cast<double>(patches.size()));
  out.max_abs_error_before_affine = max_abs_error;
  out.rmse_after_affine =
      std::sqrt(affine_sumsq / static_cast<double>(patches.size()));
  return out;
}

}  // namespace

RawDiggerPatchTable read_rawdigger_patch_table(
    const std::filesystem::path& path, std::string_view raw_filename) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("RawDigger CSV: cannot open " + path.string());
  }

  std::string line;
  if (!std::getline(is, line)) {
    throw std::runtime_error("RawDigger CSV: empty file");
  }
  const auto header = parse_csv_line(trim_cr(line));
  std::map<std::string, std::size_t> col;
  for (std::size_t i = 0; i < header.size(); ++i) {
    col.emplace(header[i], i);
  }
  const auto need = [&](const std::string& name) -> std::size_t {
    const auto it = col.find(name);
    if (it == col.end()) {
      throw std::runtime_error("RawDigger CSV: missing column " + name);
    }
    return it->second;
  };

  const std::size_t filename_i = need("Filename");
  const std::size_t sample_i = need("Sample_Name");
  const std::size_t left_i = need("Left");
  const std::size_t top_i = need("Top");
  const std::size_t width_i = need("Width");
  const std::size_t height_i = need("Height");
  const std::size_t r_i = need("Ravg");
  const std::size_t g_i = need("Gavg");
  const std::size_t b_i = need("Bavg");

  RawDiggerPatchTable out;
  std::size_t line_no = 1;
  while (std::getline(is, line)) {
    ++line_no;
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    if (fields.size() < header.size()) {
      throw std::runtime_error("RawDigger CSV: row " +
                               std::to_string(line_no) +
                               " has too few fields");
    }
    if (fields[filename_i] != raw_filename) continue;

    PatchCoord coord;
    coord.x = parse_double(fields[left_i], "row " + std::to_string(line_no)) +
              1.0;
    coord.y = parse_double(fields[top_i], "row " + std::to_string(line_no)) +
              1.0;
    coord.width = parse_double(fields[width_i], "row " +
                                                std::to_string(line_no));
    coord.height = parse_double(fields[height_i], "row " +
                                                  std::to_string(line_no));
    if (coord.width <= 0 || coord.height <= 0) {
      throw std::runtime_error(
          "RawDigger CSV: width and height must be positive");
    }

    CameraRgbPatch rgb;
    rgb.r = parse_double(fields[r_i], "row " + std::to_string(line_no));
    rgb.g = parse_double(fields[g_i], "row " + std::to_string(line_no));
    rgb.b = parse_double(fields[b_i], "row " + std::to_string(line_no));

    out.coords.push_back(coord);
    out.reference_rgb.push_back(rgb);
    out.sample_names.push_back(fields[sample_i]);
  }

  if (out.coords.empty()) {
    throw std::runtime_error("RawDigger CSV: no rows for selected raw file");
  }
  return out;
}

std::vector<PatchCoord> read_patch_coords_csv(
    const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("patch coords CSV: cannot open " + path.string());
  }

  std::vector<PatchCoord> out;
  std::string line;
  std::size_t line_no = 0;
  while (std::getline(is, line)) {
    ++line_no;
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    if (fields.size() != 4) {
      throw std::runtime_error("patch coords CSV: row " +
                               std::to_string(line_no) +
                               " must have four fields");
    }
    PatchCoord coord;
    coord.x = parse_double(fields[0], "row " + std::to_string(line_no));
    coord.y = parse_double(fields[1], "row " + std::to_string(line_no));
    coord.width = parse_double(fields[2], "row " + std::to_string(line_no));
    coord.height = parse_double(fields[3], "row " + std::to_string(line_no));
    if (coord.width <= 0 || coord.height <= 0) {
      throw std::runtime_error(
          "patch coords CSV: width and height must be positive");
    }
    out.push_back(coord);
  }
  if (out.empty()) {
    throw std::runtime_error("patch coords CSV: no coordinates");
  }
  return out;
}

std::vector<PatchMean> extract_patch_means(
    const std::vector<RgbPixel>& image, int width, int height,
    const std::vector<PatchCoord>& coords) {
  if (width <= 0 || height <= 0 ||
      image.size() != static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height)) {
    throw std::runtime_error("patch extraction: image dimensions mismatch");
  }

  std::vector<PatchMean> out;
  out.reserve(coords.size());
  for (const auto& coord : coords) {
    const int x0 = static_cast<int>(std::llround(coord.x)) - 1;
    const int y0 = static_cast<int>(std::llround(coord.y)) - 1;
    const int w = static_cast<int>(std::llround(coord.width));
    const int h = static_cast<int>(std::llround(coord.height));
    if (w <= 0 || h <= 0) {
      throw std::runtime_error("patch extraction: invalid rounded ROI size");
    }
    const int clipped_x0 = std::clamp(x0, 0, width);
    const int clipped_y0 = std::clamp(y0, 0, height);
    const int clipped_x1 = std::clamp(x0 + w, 0, width);
    const int clipped_y1 = std::clamp(y0 + h, 0, height);
    if (clipped_x1 <= clipped_x0 || clipped_y1 <= clipped_y0) {
      throw std::runtime_error("patch extraction: ROI outside image");
    }

    double sum_r = 0;
    double sum_g = 0;
    double sum_b = 0;
    std::size_t count = 0;
    for (int y = clipped_y0; y < clipped_y1; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) *
                              static_cast<std::size_t>(width);
      for (int x = clipped_x0; x < clipped_x1; ++x) {
        const RgbPixel& p = image[row + static_cast<std::size_t>(x)];
        sum_r += p.r;
        sum_g += p.g;
        sum_b += p.b;
        ++count;
      }
    }

    PatchMean patch;
    patch.source_coord = coord;
    patch.x = clipped_x0;
    patch.y = clipped_y0;
    patch.width = clipped_x1 - clipped_x0;
    patch.height = clipped_y1 - clipped_y0;
    patch.sample_count = count;
    patch.rgb.r = sum_r / static_cast<double>(count);
    patch.rgb.g = sum_g / static_cast<double>(count);
    patch.rgb.b = sum_b / static_cast<double>(count);
    out.push_back(patch);
  }
  return out;
}

std::vector<RgbPixel> apply_flat_field(
    const std::vector<RgbPixel>& image, const std::vector<RgbPixel>& flat,
    int width, int height, double floor_value,
    FlatFieldCorrectionSummary* summary) {
  if (width <= 0 || height <= 0 ||
      image.size() != static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height) ||
      flat.size() != image.size()) {
    throw std::runtime_error("flat field: image dimensions mismatch");
  }
  if (!std::isfinite(floor_value) || floor_value <= 0) {
    throw std::runtime_error("flat field: floor value must be positive");
  }

  double sum_r = 0;
  double sum_g = 0;
  double sum_b = 0;
  std::size_t valid_r = 0;
  std::size_t valid_g = 0;
  std::size_t valid_b = 0;
  std::size_t clamped = 0;
  for (const auto& p : flat) {
    if (std::isfinite(p.r) && p.r > floor_value) {
      sum_r += p.r;
      ++valid_r;
    } else {
      ++clamped;
    }
    if (std::isfinite(p.g) && p.g > floor_value) {
      sum_g += p.g;
      ++valid_g;
    } else {
      ++clamped;
    }
    if (std::isfinite(p.b) && p.b > floor_value) {
      sum_b += p.b;
      ++valid_b;
    } else {
      ++clamped;
    }
  }
  if (valid_r == 0 || valid_g == 0 || valid_b == 0) {
    throw std::runtime_error("flat field: no valid samples above floor");
  }

  const CameraRgbPatch normalizer{
      sum_r / static_cast<double>(valid_r),
      sum_g / static_cast<double>(valid_g),
      sum_b / static_cast<double>(valid_b),
  };

  std::vector<RgbPixel> corrected;
  corrected.reserve(image.size());
  for (std::size_t i = 0; i < image.size(); ++i) {
    const auto& src = image[i];
    const auto& ff = flat[i];
    const double denom_r = flat_denominator(ff.r, floor_value);
    const double denom_g = flat_denominator(ff.g, floor_value);
    const double denom_b = flat_denominator(ff.b, floor_value);
    corrected.push_back({src.r * normalizer.r / denom_r,
                         src.g * normalizer.g / denom_g,
                         src.b * normalizer.b / denom_b});
  }

  if (summary != nullptr) {
    summary->normalizer = normalizer;
    summary->floor_value = floor_value;
    summary->pixel_count = image.size();
    summary->valid_sample_count = valid_r + valid_g + valid_b;
    summary->clamped_sample_count = clamped;
  }
  return corrected;
}

std::vector<RgbPixel> apply_white_balance(const std::vector<RgbPixel>& image,
                                          WhiteBalanceGains gains) {
  if (!std::isfinite(gains.r) || !std::isfinite(gains.g) ||
      !std::isfinite(gains.b) || gains.r <= 0 || gains.g <= 0 ||
      gains.b <= 0) {
    throw std::runtime_error("white balance: gains must be positive");
  }
  std::vector<RgbPixel> out;
  out.reserve(image.size());
  for (const auto& p : image) {
    out.push_back({p.r * gains.r, p.g * gains.g, p.b * gains.b});
  }
  return out;
}

WhiteBalanceGains white_balance_gains_from_flat_field(
    const FlatFieldCorrectionSummary& flat) {
  if (!std::isfinite(flat.normalizer.r) ||
      !std::isfinite(flat.normalizer.g) ||
      !std::isfinite(flat.normalizer.b) || flat.normalizer.r <= 0 ||
      flat.normalizer.g <= 0 || flat.normalizer.b <= 0) {
    throw std::runtime_error(
        "white balance: flat-field normalizer must be positive");
  }
  return {flat.normalizer.g / flat.normalizer.r, 1.0,
          flat.normalizer.g / flat.normalizer.b};
}

PatchComparison compare_patch_means_to_rgb(
    const std::vector<PatchMean>& patches,
    const std::vector<CameraRgbPatch>& reference_rgb) {
  if (patches.size() != reference_rgb.size()) {
    throw std::runtime_error(
        "patch comparison: patch and reference counts differ");
  }
  if (patches.size() < 2) {
    throw std::runtime_error("patch comparison: at least two patches required");
  }

  PatchComparison out;
  out.patch_count = patches.size();
  for (int component = 0; component < 3; ++component) {
    out.channels[static_cast<std::size_t>(component)] =
        compare_channel(patches, reference_rgb, component);
  }
  return out;
}

PatchLocalizationValidation validate_patch_localization_against_oracle(
    const std::vector<PatchMean>& patches, const RawDiggerPatchTable& oracle,
    PatchLocalizationValidationThresholds thresholds) {
  if (patches.size() != oracle.coords.size() ||
      patches.size() != oracle.reference_rgb.size()) {
    throw std::runtime_error(
        "localization validation: patch and RawDigger oracle counts differ");
  }
  if (patches.size() < 2) {
    throw std::runtime_error(
        "localization validation: at least two patches required");
  }
  if (!std::isfinite(thresholds.max_center_error_px) ||
      thresholds.max_center_error_px < 0 ||
      !std::isfinite(thresholds.min_channel_correlation) ||
      !std::isfinite(thresholds.max_abs_mean_error_dn) ||
      thresholds.max_abs_mean_error_dn < 0) {
    throw std::runtime_error(
        "localization validation: invalid threshold");
  }

  PatchLocalizationValidation out;
  out.patch_count = patches.size();
  out.thresholds = thresholds;
  out.patch_count_gate_passes =
      patches.size() == thresholds.expected_patch_count;

  double center_sumsq = 0;
  out.center_residuals.reserve(patches.size());
  for (std::size_t i = 0; i < patches.size(); ++i) {
    const double generated_center_x = patch_center_x(patches[i].source_coord);
    const double generated_center_y = patch_center_y(patches[i].source_coord);
    const double oracle_center_x = patch_center_x(oracle.coords[i]);
    const double oracle_center_y = patch_center_y(oracle.coords[i]);
    const double dx = generated_center_x - oracle_center_x;
    const double dy = generated_center_y - oracle_center_y;
    const double error = std::sqrt(dx * dx + dy * dy);
    out.max_center_error_px = std::max(out.max_center_error_px, error);
    center_sumsq += error * error;
    out.center_residuals.push_back(
        PatchCenterResidual{reference_patch_id_from_index(i),
                            reference_patch_row_from_index(i),
                            reference_patch_column_from_index(i),
                            generated_center_x,
                            generated_center_y,
                            oracle_center_x,
                            oracle_center_y,
                            dx,
                            dy,
                            error});
  }
  out.rms_center_error_px =
      std::sqrt(center_sumsq / static_cast<double>(patches.size()));
  out.center_gate_passes =
      out.max_center_error_px <= thresholds.max_center_error_px;

  out.rgb_comparison = compare_patch_means_to_rgb(patches, oracle.reference_rgb);
  out.correlation_gate_passes = true;
  out.mean_error_gate_passes = true;
  for (const auto& channel : out.rgb_comparison.channels) {
    if (channel.correlation < thresholds.min_channel_correlation) {
      out.correlation_gate_passes = false;
    }
    if (channel.max_abs_error_before_affine >
        thresholds.max_abs_mean_error_dn) {
      out.mean_error_gate_passes = false;
    }
  }
  out.passes = out.patch_count_gate_passes && out.center_gate_passes &&
               out.correlation_gate_passes && out.mean_error_gate_passes;
  return out;
}

void write_camera_rgb_csv(std::ostream& os,
                          const std::vector<PatchMean>& patches) {
  os << std::setprecision(17);
  for (const auto& patch : patches) {
    os << patch.rgb.r << ',' << patch.rgb.g << ',' << patch.rgb.b << '\n';
  }
}

std::string_view flat_field_normalization_policy() {
  return kFlatFieldNormalizationPolicy;
}

double flat_field_near_ceiling_threshold_fraction() {
  return kFlatNearCeilingFraction;
}

void write_patch_report_json(
    std::ostream& os, std::string_view file_label,
    std::string_view coords_label, std::string_view coordinate_source_format,
    const RawMeta& meta, int width, int height, std::string_view flat_label,
    const std::optional<FlatFieldCorrectionSummary>& flat,
    const std::optional<WhiteBalanceGains>& wb, std::string_view wb_policy,
    const std::vector<PatchMean>& patches,
    const std::vector<std::string>& sample_names,
    const std::optional<PatchComparison>& comparison,
    std::string_view reference_label,
    const std::optional<PatchGeometryReport>& geometry,
    const std::optional<SpectralReferenceOrientationReport>& orientation,
    const std::optional<PatchLocalizationValidation>& localization) {
  JsonWriter w(os);
  w.begin_object();
  w.key("file");
  w.value(file_label);
  w.key("coords_path");
  w.value(coords_label);
  w.key("coordinate_source_format");
  w.value(coordinate_source_format);
  w.key("generated_chart_geometry");
  write_generated_geometry(w, geometry);
  w.key("orientation_validation");
  write_orientation_validation(w, orientation);
  w.key("localization_validation");
  write_localization_validation(w, localization);
  w.key("extraction_coordinate_convention");
  w.value("one_based_top_left_rectangles_after_source_conversion");
  w.key("rgb_source");
  w.value("bilinear_demosaic_black_subtracted_raw");
  w.key("corrections");
  write_corrections(w, flat_label, flat, wb, wb_policy);
  w.key("camera");
  w.begin_object();
  w.key("make");
  w.value(meta.make);
  w.key("model");
  w.value(meta.model);
  w.key("cfa_pattern");
  w.value(meta.cfa_pattern);
  w.key("black_level");
  w.value(meta.black_level);
  w.key("white_level");
  w.value(meta.white_level);
  w.end_object();
  w.key("image");
  w.begin_object();
  w.key("width");
  w.value(width);
  w.key("height");
  w.value(height);
  w.key("algorithm");
  w.value("bilinear");
  w.end_object();
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(patches.size()));
  w.key("patches");
  w.begin_array();
  for (std::size_t i = 0; i < patches.size(); ++i) {
    write_patch(w, patches[i], i, sample_names);
  }
  w.end_array();
  w.key("comparison");
  if (comparison) {
    write_comparison(w, *comparison, reference_label);
  } else {
    w.null();
  }
  w.key("limitations");
  w.begin_array();
  w.value("Bilinear demosaic only; not LibRaw/AHD or production ISP color");
  if (!flat) {
    w.value("No sphere flat-field correction was applied");
  }
  if (!wb) {
    w.value("No white-balance gains were applied");
  }
  if (comparison && (flat || wb)) {
    w.value(
        "Reference RGB comparison uses caller-supplied values; they must match "
        "the applied correction state");
  }
  w.value("Reference RGB comparison is affine/correlation validation, not DeltaE");
  w.end_array();
  w.end_object();
}

}  // namespace camera_iq
