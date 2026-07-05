#include "camera_iq/patches.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
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

  double sumsq = 0;
  for (std::size_t i = 0; i < patches.size(); ++i) {
    const double predicted =
        out.slope * rgb_component(patches[i].rgb, component) + out.intercept;
    const double residual = predicted - rgb_component(ref[i], component);
    sumsq += residual * residual;
  }
  out.rmse_after_affine =
      std::sqrt(sumsq / static_cast<double>(patches.size()));
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

}  // namespace camera_iq
