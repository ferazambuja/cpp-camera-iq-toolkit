#pragma once

#include <array>
#include <filesystem>
#include <limits>
#include <vector>

#include "camera_iq/color_reference.hpp"

namespace camera_iq {

struct Xyz {
  double x = 0;
  double y = 0;
  double z = 0;
};

struct Lab {
  double l = 0;
  double a = 0;
  double b = 0;
};

struct RenderedReference {
  Xyz white_xyz;
  std::vector<Xyz> patch_xyz;
};

struct CcmFit {
  std::array<std::array<double, 3>, 3> matrix{};
  std::size_t patch_count = 0;
  double mean_delta_e_76 = 0;
  double max_delta_e_76 = 0;
  double rms_delta_e_76 = 0;
  double mean_delta_e_2000 = 0;
  double max_delta_e_2000 = 0;
  double rms_delta_e_2000 = 0;
};

struct CcmCrossValidation {
  std::size_t patch_count = 0;
  std::size_t fold_count = 0;
  double mean_delta_e_76 = 0;
  double max_delta_e_76 = 0;
  double rms_delta_e_76 = 0;
  double mean_delta_e_2000 = 0;
  double max_delta_e_2000 = 0;
  double rms_delta_e_2000 = 0;
};

struct CcmEvaluation {
  std::size_t patch_count = 0;
  double mean_delta_e_76 = 0;
  double max_delta_e_76 = 0;
  double rms_delta_e_76 = 0;
  double mean_delta_e_2000 = 0;
  double max_delta_e_2000 = 0;
  double rms_delta_e_2000 = 0;
};

struct CcmLightnessSelection {
  double max_lstar = 0;
  std::vector<std::size_t> kept_indices;
  std::vector<std::size_t> excluded_indices;
};

struct CcmDarkPatchDiagnostics {
  double max_lstar = 25.0;
  std::size_t patch_count = 0;
  std::size_t worst_patch_index = std::numeric_limits<std::size_t>::max();
  double mean_delta_e_76 = 0;
  double max_delta_e_76 = 0;
  double rms_delta_e_76 = 0;
  double mean_delta_e_2000 = 0;
  double max_delta_e_2000 = 0;
  double rms_delta_e_2000 = 0;
};

RenderedReference render_reference_xyz(const SpectralReference& ref,
                                       const std::vector<double>& illuminant);

std::vector<double> read_spectrum_csv_interpolated(
    const std::filesystem::path& path,
    const std::vector<double>& target_wavelengths_nm);

Lab xyz_to_lab(const Xyz& xyz, const Xyz& white);

double delta_e_2000(const Lab& a, const Lab& b);

Xyz apply_ccm(const std::array<std::array<double, 3>, 3>& matrix,
              const CameraRgbPatch& rgb);

CcmFit fit_rgb_to_xyz_ccm(const std::vector<CameraRgbPatch>& camera_rgb,
                          const std::vector<Xyz>& target_xyz,
                          const Xyz& white_xyz);

CcmEvaluation evaluate_rgb_to_xyz_ccm(
    const std::array<std::array<double, 3>, 3>& matrix,
    const std::vector<CameraRgbPatch>& camera_rgb,
    const std::vector<Xyz>& target_xyz, const Xyz& white_xyz);

CcmCrossValidation cross_validate_rgb_to_xyz_ccm(
    const std::vector<CameraRgbPatch>& camera_rgb,
    const std::vector<Xyz>& target_xyz, const Xyz& white_xyz,
    std::size_t fold_count = 5);

CcmLightnessSelection select_reference_lightness(
    const std::vector<Xyz>& target_xyz, const Xyz& white_xyz,
    double exclude_below_lstar);

CcmDarkPatchDiagnostics diagnose_dark_patches(
    const std::vector<CameraRgbPatch>& camera_rgb,
    const std::vector<Xyz>& target_xyz, const Xyz& white_xyz,
    const std::array<std::array<double, 3>, 3>& matrix,
    double max_lstar = 25.0);

}  // namespace camera_iq
