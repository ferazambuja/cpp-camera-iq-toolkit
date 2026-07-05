#include "camera_iq/patches.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace camera_iq {
namespace {

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

double flat_denominator(double value, double floor_value) {
  if (!std::isfinite(value) || value <= floor_value) return floor_value;
  return value;
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

void write_camera_rgb_csv(std::ostream& os,
                          const std::vector<PatchMean>& patches) {
  os << std::setprecision(17);
  for (const auto& patch : patches) {
    os << patch.rgb.r << ',' << patch.rgb.g << ',' << patch.rgb.b << '\n';
  }
}

}  // namespace camera_iq
