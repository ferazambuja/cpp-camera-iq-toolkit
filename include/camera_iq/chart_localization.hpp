#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/patches.hpp"

namespace camera_iq {

struct Point2d {
  double x = 0;
  double y = 0;
};

struct ChartCorners {
  Point2d top_left;
  Point2d top_right;
  Point2d bottom_right;
  Point2d bottom_left;
};

struct ChartPatchGeometry {
  std::string reference_patch_id;
  int row = 0;
  int column = 0;
  PatchCoord extraction_coord;
};

struct ChartLocalizationResult {
  std::string chart_model = "ColorChecker-SG 14x10";
  std::string method = "corner_seeded_projective_grid";
  ChartCorners corners;
  std::vector<ChartPatchGeometry> patches;
};

ChartLocalizationResult localize_colorchecker_sg_grid(
    const ChartCorners& corners, double inner_fraction = 0.65);

ChartCorners parse_colorchecker_sg_corners(std::string_view text);

std::vector<PatchCoord> patch_coords_from_chart_geometry(
    const ChartLocalizationResult& geometry);

std::vector<std::string> patch_ids_from_chart_geometry(
    const ChartLocalizationResult& geometry);

}  // namespace camera_iq
