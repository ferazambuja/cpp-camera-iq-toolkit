#pragma once

#include <array>
#include <string>
#include <vector>

namespace camera_iq {

// Inputs for the camera Sensitivity Metamerism Index (SMI), evaluated in the
// style of ISO 17321: synthesize the camera's linear RGB response to a set of
// test colours under a reference illuminant, fit the optimal 3x3 RGB->XYZ
// transform, and measure the residual CIELAB colour error against the true
// colorimetry of the same test colours. All spectral vectors share grid_nm.
struct SpectralSmiInputs {
  std::vector<double> grid_nm;                    // shared wavelength grid (nm)
  std::vector<double> illuminant;                 // E(lambda) on grid
  std::array<std::vector<double>, 3> cmf;         // xbar, ybar, zbar on grid
  std::array<std::vector<double>, 3> ssf;         // camera R, G, B on grid
  std::vector<std::vector<double>> reflectance;   // [patch][grid] test colours
  double smi_slope = 5.5;                         // SMI = 100 - slope * meanDE76
};

struct SpectralSmiResult {
  std::string method;
  std::size_t patch_count = 0;
  double mean_delta_e_76 = 0;
  double max_delta_e_76 = 0;
  double rms_delta_e_76 = 0;
  double mean_delta_e_2000 = 0;
  double max_delta_e_2000 = 0;
  double rms_delta_e_2000 = 0;
  double smi = 0;          // 100 - smi_slope * mean_delta_e_76
  double smi_slope = 5.5;
  std::array<std::array<double, 3>, 3> matrix{};  // fitted RGB->XYZ transform
};

// Throws std::runtime_error on grid/size mismatch, fewer than three test
// colours, or a degenerate (non-positive white Y) illuminant/CMF pairing.
SpectralSmiResult compute_spectral_smi(const SpectralSmiInputs& in);

}  // namespace camera_iq
