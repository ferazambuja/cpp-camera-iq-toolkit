#pragma once

#include <vector>

#include "camera_iq/chart_localization.hpp"
#include "camera_iq/patches.hpp"

namespace camera_iq {

LocalizationModelComparison analyze_localization_residual_models(
    const std::vector<PatchCenterResidual>& residuals, Point2d stated_center);

std::vector<IndependentPatchCenter> estimate_patch_centers_by_color_centroid(
    const std::vector<RgbPixel>& image, int width, int height,
    const std::vector<PatchCoord>& coords, double search_scale = 2.5);

struct IndependentCenterRepeatability {
  std::size_t valid_count = 0;
  double rms_px = 0;
};

IndependentCenterRepeatability estimate_independent_center_repeatability(
    const std::vector<IndependentPatchCenter>& a,
    const std::vector<IndependentPatchCenter>& b);

LocalizationIndependentCenterCheck compare_independent_patch_centers(
    const std::vector<PatchCoord>& generated_coords,
    const RawDiggerPatchTable& oracle,
    const std::vector<IndependentPatchCenter>& independent);

LocalizationIndependentCenterCheck compare_dual_seed_independent_patch_centers(
    const std::vector<PatchCoord>& generated_coords,
    const RawDiggerPatchTable& oracle,
    const std::vector<IndependentPatchCenter>& generated_seeded,
    const std::vector<IndependentPatchCenter>& oracle_seeded);

void finalize_localization_model_comparison(
    LocalizationModelComparison& comparison,
    const LocalizationIndependentCenterCheck& independent);

}  // namespace camera_iq
