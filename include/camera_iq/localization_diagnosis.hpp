#pragma once

#include <vector>

#include "camera_iq/chart_localization.hpp"
#include "camera_iq/patches.hpp"

namespace camera_iq {

LocalizationModelComparison analyze_localization_residual_models(
    const std::vector<PatchCenterResidual>& residuals, Point2d stated_center);

std::vector<IndependentPatchCenter> estimate_patch_centers_by_color_centroid(
    const std::vector<RgbPixel>& image, int width, int height,
    const std::vector<PatchCoord>& coords);

LocalizationIndependentCenterCheck compare_independent_patch_centers(
    const std::vector<PatchCoord>& generated_coords,
    const RawDiggerPatchTable& oracle,
    const std::vector<IndependentPatchCenter>& independent);

}  // namespace camera_iq
