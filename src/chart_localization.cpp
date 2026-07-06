#include "camera_iq/chart_localization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace camera_iq {
namespace {

constexpr int kRows = 10;
constexpr int kColumns = 14;
constexpr double kChartWidth = 24.20;
constexpr double kChartHeight = 17.20;
constexpr double kPatchSize = 1.45;
constexpr double kPatchPitch = 1.75;
constexpr double kEpsilon = 1e-12;

using Matrix8 = std::array<std::array<double, 9>, 8>;
using Homography = std::array<double, 8>;

bool finite(Point2d p) { return std::isfinite(p.x) && std::isfinite(p.y); }

Point2d subtract(Point2d a, Point2d b) { return {a.x - b.x, a.y - b.y}; }

double cross(Point2d a, Point2d b) { return a.x * b.y - a.y * b.x; }

double cross_at(Point2d a, Point2d b, Point2d c) {
  return cross(subtract(b, a), subtract(c, b));
}

double polygon_area2(const std::array<Point2d, 4>& points) {
  double sum = 0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto& a = points[i];
    const auto& b = points[(i + 1) % points.size()];
    sum += a.x * b.y - b.x * a.y;
  }
  return sum;
}

void validate_corners(const ChartCorners& corners, double inner_fraction) {
  if (!(inner_fraction > 0.0 && inner_fraction <= 1.0) ||
      !std::isfinite(inner_fraction)) {
    throw std::runtime_error(
        "ColorChecker-SG inner ROI fraction must be finite and in (0, 1]");
  }

  const std::array<Point2d, 4> points = {
      corners.top_left, corners.top_right, corners.bottom_right,
      corners.bottom_left};
  for (const auto& point : points) {
    if (!finite(point)) {
      throw std::runtime_error("ColorChecker-SG corners must be finite");
    }
  }

  const double area2 = polygon_area2(points);
  if (std::abs(area2) < kEpsilon) {
    throw std::runtime_error("ColorChecker-SG corners are degenerate");
  }

  const double sign = area2 > 0.0 ? 1.0 : -1.0;
  for (std::size_t i = 0; i < points.size(); ++i) {
    const double turn =
        cross_at(points[i], points[(i + 1) % points.size()],
                 points[(i + 2) % points.size()]);
    if (sign * turn <= kEpsilon) {
      throw std::runtime_error(
          "ColorChecker-SG corners must form a non-crossed convex quadrilateral");
    }
  }
}

void add_homography_rows(Matrix8& m, int row, Point2d src, Point2d dst) {
  m[static_cast<std::size_t>(row)] = {
      src.x, src.y, 1.0, 0.0,   0.0,   0.0,
      -dst.x * src.x, -dst.x * src.y, dst.x};
  m[static_cast<std::size_t>(row + 1)] = {
      0.0,   0.0,   0.0, src.x, src.y, 1.0,
      -dst.y * src.x, -dst.y * src.y, dst.y};
}

Homography solve_homography(const ChartCorners& corners) {
  Matrix8 m{};
  add_homography_rows(m, 0, {0.0, 0.0}, corners.top_left);
  add_homography_rows(m, 2, {kChartWidth, 0.0}, corners.top_right);
  add_homography_rows(m, 4, {kChartWidth, kChartHeight},
                      corners.bottom_right);
  add_homography_rows(m, 6, {0.0, kChartHeight}, corners.bottom_left);

  for (std::size_t col = 0; col < 8; ++col) {
    std::size_t pivot = col;
    double pivot_abs = std::abs(m[col][col]);
    for (std::size_t row = col + 1; row < 8; ++row) {
      const double candidate = std::abs(m[row][col]);
      if (candidate > pivot_abs) {
        pivot = row;
        pivot_abs = candidate;
      }
    }
    if (pivot_abs < kEpsilon) {
      throw std::runtime_error("ColorChecker-SG homography solve failed");
    }
    if (pivot != col) {
      std::swap(m[pivot], m[col]);
    }

    const double scale = m[col][col];
    for (std::size_t k = col; k < 9; ++k) {
      m[col][k] /= scale;
    }
    for (std::size_t row = 0; row < 8; ++row) {
      if (row == col) {
        continue;
      }
      const double factor = m[row][col];
      for (std::size_t k = col; k < 9; ++k) {
        m[row][k] -= factor * m[col][k];
      }
    }
  }

  Homography h{};
  for (std::size_t i = 0; i < 8; ++i) {
    h[i] = m[i][8];
  }
  return h;
}

Point2d project(const Homography& h, Point2d p) {
  const double denom = h[6] * p.x + h[7] * p.y + 1.0;
  if (!std::isfinite(denom) || std::abs(denom) < kEpsilon) {
    throw std::runtime_error("ColorChecker-SG homography projection failed");
  }
  const Point2d out{(h[0] * p.x + h[1] * p.y + h[2]) / denom,
                    (h[3] * p.x + h[4] * p.y + h[5]) / denom};
  if (!finite(out)) {
    throw std::runtime_error("ColorChecker-SG homography projection failed");
  }
  return out;
}

std::string patch_id(int row, int column) {
  std::string id(1, static_cast<char>('A' + column));
  id += std::to_string(row + 1);
  return id;
}

double parse_number(std::string_view field, std::string_view context) {
  const std::string text(field);
  try {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (...) {
    throw std::runtime_error("invalid ColorChecker-SG corner number in " +
                             std::string(context));
  }
}

Point2d parse_point(std::string_view field, int index) {
  const auto comma = field.find(',');
  if (comma == std::string_view::npos ||
      field.find(',', comma + 1) != std::string_view::npos) {
    throw std::runtime_error(
        "ColorChecker-SG corners must use x,y point pairs");
  }
  const std::string context = "corner " + std::to_string(index + 1);
  return {parse_number(field.substr(0, comma), context),
          parse_number(field.substr(comma + 1), context)};
}

PatchCoord projected_roi(const Homography& h, double left, double top,
                         double right, double bottom) {
  const std::array<Point2d, 4> projected = {
      project(h, {left, top}), project(h, {right, top}),
      project(h, {right, bottom}), project(h, {left, bottom})};
  double min_x = projected[0].x;
  double max_x = projected[0].x;
  double min_y = projected[0].y;
  double max_y = projected[0].y;
  for (const auto& point : projected) {
    min_x = std::min(min_x, point.x);
    max_x = std::max(max_x, point.x);
    min_y = std::min(min_y, point.y);
    max_y = std::max(max_y, point.y);
  }
  if (!(max_x > min_x && max_y > min_y)) {
    throw std::runtime_error("ColorChecker-SG projected ROI is degenerate");
  }
  return PatchCoord{min_x + 1.0, min_y + 1.0, max_x - min_x, max_y - min_y};
}

}  // namespace

ChartLocalizationResult localize_colorchecker_sg_grid(
    const ChartCorners& corners, double inner_fraction) {
  validate_corners(corners, inner_fraction);
  const auto h = solve_homography(corners);

  ChartLocalizationResult result;
  result.corners = corners;
  result.patches.reserve(kRows * kColumns);

  const double roi_size = kPatchSize * inner_fraction;
  const double half_roi = roi_size / 2.0;
  for (int row = 0; row < kRows; ++row) {
    for (int column = 0; column < kColumns; ++column) {
      const double patch_left = column * kPatchPitch;
      const double patch_top = row * kPatchPitch;
      const double center_x = patch_left + kPatchSize / 2.0;
      const double center_y = patch_top + kPatchSize / 2.0;

      ChartPatchGeometry patch;
      patch.reference_patch_id = patch_id(row, column);
      patch.row = row;
      patch.column = column;
      patch.extraction_coord =
          projected_roi(h, center_x - half_roi, center_y - half_roi,
                        center_x + half_roi, center_y + half_roi);
      result.patches.push_back(patch);
    }
  }

  return result;
}

ChartCorners parse_colorchecker_sg_corners(std::string_view text) {
  std::vector<Point2d> points;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t end = text.find(';', start);
    const std::string_view field =
        end == std::string_view::npos ? text.substr(start)
                                      : text.substr(start, end - start);
    if (field.empty()) {
      throw std::runtime_error(
          "ColorChecker-SG corners must contain four x,y point pairs");
    }
    points.push_back(parse_point(field, static_cast<int>(points.size())));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  if (points.size() != 4) {
    throw std::runtime_error(
        "ColorChecker-SG corners must contain exactly four points");
  }
  return {points[0], points[1], points[2], points[3]};
}

std::vector<PatchCoord> patch_coords_from_chart_geometry(
    const ChartLocalizationResult& geometry) {
  std::vector<PatchCoord> coords;
  coords.reserve(geometry.patches.size());
  for (const auto& patch : geometry.patches) {
    coords.push_back(patch.extraction_coord);
  }
  return coords;
}

std::vector<std::string> patch_ids_from_chart_geometry(
    const ChartLocalizationResult& geometry) {
  std::vector<std::string> ids;
  ids.reserve(geometry.patches.size());
  for (const auto& patch : geometry.patches) {
    ids.push_back(patch.reference_patch_id);
  }
  return ids;
}

}  // namespace camera_iq
