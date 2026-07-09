#pragma once

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

struct StepchartLocalizationResult {
  std::string chart_model = "OECF Stepchart 20-zone strip";
  std::string method = "corner_seeded_projective_strip";
  ChartCorners corners;
  std::vector<StepchartZoneGeometry> zones;
};

StepchartLocalizationResult localize_stepchart_20_zone_strip(
    const ChartCorners& corners, double inner_fraction = 0.65);

ChartCorners parse_stepchart_strip_corners(std::string_view text);

std::vector<PatchCoord> patch_coords_from_stepchart_geometry(
    const StepchartLocalizationResult& geometry);

}  // namespace camera_iq
