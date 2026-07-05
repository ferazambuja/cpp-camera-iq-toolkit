#pragma once

#include <array>
#include <filesystem>
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
};

RenderedReference render_reference_xyz(const SpectralReference& ref,
                                       const std::vector<double>& illuminant);

std::vector<double> read_spectrum_csv_interpolated(
    const std::filesystem::path& path,
    const std::vector<double>& target_wavelengths_nm);

Lab xyz_to_lab(const Xyz& xyz, const Xyz& white);

Xyz apply_ccm(const std::array<std::array<double, 3>, 3>& matrix,
              const CameraRgbPatch& rgb);

CcmFit fit_rgb_to_xyz_ccm(const std::vector<CameraRgbPatch>& camera_rgb,
                          const std::vector<Xyz>& target_xyz,
                          const Xyz& white_xyz);

}  // namespace camera_iq
