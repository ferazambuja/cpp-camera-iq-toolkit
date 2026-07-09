#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/chart_localization.hpp"
#include "camera_iq/patches.hpp"

namespace camera_iq {

struct StepchartZoneGeometry {
  int zone = 0;
  int row = 0;
  int column = 0;
  PatchCoord extraction_coord;
};

struct StepchartRingSeed {
  Point2d center;
  double radius = 0.0;
  double theta1_degrees = 0.0;
};

struct StepchartLocalizationResult {
  std::string chart_model = "OECF Stepchart 20-zone strip";
  std::string method = "corner_seeded_projective_strip";
  ChartCorners corners;
  std::optional<StepchartRingSeed> ring_seed;
  double inner_fraction = 0.0;
  double roi_size_px = 0.0;
  std::vector<StepchartZoneGeometry> zones;
};

StepchartLocalizationResult localize_stepchart_20_zone_strip(
    const ChartCorners& corners, double inner_fraction = 0.65);

StepchartLocalizationResult localize_stepchart_20_zone_ring(
    const StepchartRingSeed& seed, double roi_size_px = 150.0);

ChartCorners parse_stepchart_strip_corners(std::string_view text);

StepchartRingSeed parse_stepchart_ring_seed(std::string_view text);

std::vector<PatchCoord> patch_coords_from_stepchart_geometry(
    const StepchartLocalizationResult& geometry);

}  // namespace camera_iq
