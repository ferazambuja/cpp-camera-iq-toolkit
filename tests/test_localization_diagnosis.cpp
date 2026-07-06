#include "camera_iq/localization_diagnosis.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "camera_iq/patches.hpp"
#include "harness.hpp"

using camera_iq::CameraRgbPatch;
using camera_iq::PatchCoord;
using camera_iq::PatchCenterResidual;
using camera_iq::RawDiggerPatchTable;
using camera_iq::RgbPixel;
using camera_iq::Point2d;
using camera_iq::analyze_localization_residual_models;
using camera_iq::compare_independent_patch_centers;
using camera_iq::estimate_patch_centers_by_color_centroid;
using test::check;
using test::check_near;

namespace {

std::string patch_id(int row, int column) {
  std::string id(1, static_cast<char>('A' + column));
  id += std::to_string(row + 1);
  return id;
}

std::vector<PatchCenterResidual> synthetic_bow_residuals() {
  std::vector<PatchCenterResidual> out;
  out.reserve(140);
  for (int row = 0; row < 10; ++row) {
    for (int column = 0; column < 14; ++column) {
      const double u = (static_cast<double>(column) - 6.5) / 6.5;
      const double x = 1250.0 + column * 275.0;
      const double y = 720.0 + row * 280.0;
      const double q = 1.0 - u * u;
      const double dx = -14.0 * q;
      const double dy = 2.5 * q;
      PatchCenterResidual r;
      r.reference_patch_id = patch_id(row, column);
      r.row = row;
      r.column = column;
      r.generated_center_x = x;
      r.generated_center_y = y;
      r.oracle_center_x = x - dx;
      r.oracle_center_y = y - dy;
      r.dx_px = dx;
      r.dy_px = dy;
      r.distance_px = std::sqrt(dx * dx + dy * dy);
      out.push_back(r);
    }
  }
  return out;
}

const camera_iq::LocalizationModelReport& find_model(
    const camera_iq::LocalizationModelComparison& comparison,
    const std::string& name) {
  const auto it = std::find_if(
      comparison.models.begin(), comparison.models.end(),
      [&](const auto& model) { return model.name == name; });
  check(it != comparison.models.end(), "diagnosis: required model exists");
  return *it;
}

const camera_iq::LocalizationHoldoutScore& find_split(
    const camera_iq::LocalizationModelReport& model,
    const std::string& split) {
  const auto it = std::find_if(
      model.heldout_scores.begin(), model.heldout_scores.end(),
      [&](const auto& score) { return score.split == split; });
  check(it != model.heldout_scores.end(),
        "diagnosis: required spatial split exists");
  return *it;
}

}  // namespace

void TESTS() {
  const auto residuals = synthetic_bow_residuals();
  const auto comparison =
      analyze_localization_residual_models(residuals, Point2d{3008.0, 2007.0});

  check(comparison.diagnostic_only,
        "diagnosis: model comparison is diagnostic-only");
  check(!comparison.predeclared_gate_revision,
        "diagnosis: model comparison does not revise the localization gate");
  check(comparison.patch_count == 140, "diagnosis: all 140 residuals used");
  check(comparison.radial_affine_baselines_reported,
        "diagnosis: radial and affine baselines are explicitly reported");
  check(comparison.observed_anisotropy_dx_over_dy > 4.0,
        "diagnosis: observed dx/dy anisotropy recorded");
  check(comparison.isotropic_radial_predicted_anisotropy_dx_over_dy > 1.1,
        "diagnosis: radial anisotropy baseline is geometry-predicted, not 1");

  const auto& zero = find_model(comparison, "corner_seeded_homography_baseline");
  check(zero.degrees_of_freedom == 0, "diagnosis: zero baseline DOF");
  const auto& radial =
      find_model(comparison, "isotropic_radial_k1_baseline");
  check(radial.degrees_of_freedom == 1,
        "diagnosis: radial baseline reports one DOF");
  const auto& affine =
      find_model(comparison, "affine_linear_pitch_baseline");
  check(affine.degrees_of_freedom == 6,
        "diagnosis: affine baseline reports six DOF");
  const auto& cylindrical =
      find_model(comparison, "chart_cylindrical_bow_candidate");
  check(cylindrical.degrees_of_freedom == 3,
        "diagnosis: cylindrical chart candidate reports parsimonious DOF");
  const auto& warp =
      find_model(comparison, "smooth_polynomial_degree2_warp_candidate");
  check(warp.degrees_of_freedom == 12,
        "diagnosis: smooth warp candidate reports fixed polynomial DOF");

  for (const auto* model : {&zero, &radial, &affine, &cylindrical, &warp}) {
    check(find_split(*model, "checkerboard").folds >= 2,
          "diagnosis: checkerboard held-out split reported");
    check(find_split(*model, "row_block").folds >= 5,
          "diagnosis: row-block held-out split reported");
    check(find_split(*model, "column_block").folds >= 7,
          "diagnosis: column-block held-out split reported");
  }

  const double affine_rms = find_split(affine, "checkerboard").metrics.rms_px;
  const double radial_rms = find_split(radial, "checkerboard").metrics.rms_px;
  const double cylindrical_rms =
      find_split(cylindrical, "checkerboard").metrics.rms_px;
  check(cylindrical_rms < 1e-8,
        "diagnosis: cylindrical candidate predicts synthetic bow held-out");
  check(affine_rms > 3.0,
        "diagnosis: affine baseline remains a reported loser, not pruned");
  check(radial_rms > 3.0,
        "diagnosis: radial baseline remains a reported loser, not pruned");
  check(find_split(warp, "checkerboard").metrics.rms_px < 1e-8,
        "diagnosis: high-DOF smooth warp can fit but is reported with DOF");

  check(comparison.identifiability_note.find("off-centre") !=
            std::string::npos,
        "diagnosis: centered-capture confound is reported");

  std::vector<PatchCenterResidual> too_few(residuals.begin(),
                                          residuals.begin() + 5);
  bool threw = false;
  try {
    (void)analyze_localization_residual_models(too_few,
                                               Point2d{3008.0, 2007.0});
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "diagnosis: rejects incomplete residual sets");

  std::vector<RgbPixel> image(40 * 30, RgbPixel{0, 0, 0});
  for (int y = 9; y < 19; ++y) {
    for (int x = 12; x < 22; ++x) {
      image[static_cast<std::size_t>(y * 40 + x)] = {100, 120, 140};
    }
  }
  const std::vector<PatchCoord> generated_coords = {PatchCoord{13, 11, 4, 4}};
  const auto centres =
      estimate_patch_centers_by_color_centroid(image, 40, 30, generated_coords);
  check(centres.size() == 1 && centres[0].valid,
        "independent center: color centroid produces a valid estimate");
  check_near(centres[0].x, 17.0, 0.25,
             "independent center: x follows the patch blob, not ROI center");
  check_near(centres[0].y, 14.0, 0.25,
             "independent center: y follows the patch blob, not ROI center");

  RawDiggerPatchTable oracle;
  oracle.coords = {PatchCoord{14, 11, 8, 8}};
  oracle.reference_rgb = {CameraRgbPatch{1, 2, 3}};
  const auto independent_check =
      compare_independent_patch_centers(generated_coords, oracle, centres);
  check(independent_check.attempted,
        "independent center: comparison records an attempted third source");
  check(independent_check.valid_count == 1,
        "independent center: comparison counts valid estimates");
  check(independent_check.rawdigger_oracle_rms_px <
            independent_check.generated_grid_rms_px,
        "independent center: comparison distinguishes closer source");
  check(independent_check.tracks == "rawdigger_oracle",
        "independent center: interpretation tracks the closer RawDigger source");
}
