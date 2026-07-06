#pragma once

#include <vector>

#include "camera_iq/chart_localization.hpp"
#include "camera_iq/patches.hpp"

namespace camera_iq {

LocalizationModelComparison analyze_localization_residual_models(
    const std::vector<PatchCenterResidual>& residuals, Point2d stated_center);

// Estimates patch centres from image content, but the search window and
// reference colour are seeded by the supplied coordinates. This is a seeded
// refinement, not blind chart localization.
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

// Single-seed diagnostic helper. Because the detector is anchored by the
// supplied coordinates, this function must not be used as the final
// grid-vs-RawDigger arbiter. Use compare_dual_seed_independent_patch_centers()
// for arbitration.
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
