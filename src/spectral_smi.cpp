#include "camera_iq/spectral_smi.hpp"

#include <stdexcept>
#include <string>

#include "camera_iq/color_reference.hpp"  // CameraRgbPatch
#include "camera_iq/colorimetry.hpp"      // Xyz, CcmFit, fit_rgb_to_xyz_ccm

namespace camera_iq {
namespace {

// Trapezoidal integration weights over a (not necessarily uniform) grid,
// matching integrate_xyz() in colorimetry.cpp so the synthesized camera RGB and
// the true XYZ are integrated on identical footing.
std::vector<double> trapezoid_weights(const std::vector<double>& g) {
  const std::size_t n = g.size();
  std::vector<double> w(n, 0.0);
  if (n == 1) {
    w[0] = 1.0;
    return w;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (i == 0) {
      w[i] = (g[1] - g[0]) * 0.5;
    } else if (i + 1 == n) {
      w[i] = (g[i] - g[i - 1]) * 0.5;
    } else {
      w[i] = (g[i + 1] - g[i - 1]) * 0.5;
    }
  }
  return w;
}

void require_len(const std::vector<double>& v, std::size_t n, const char* what) {
  if (v.size() != n)
    throw std::runtime_error(std::string("spectral smi: ") + what +
                             " length does not match grid");
}

}  // namespace

SpectralSmiResult compute_spectral_smi(const SpectralSmiInputs& in) {
  const std::size_t n = in.grid_nm.size();
  if (n < 2)
    throw std::runtime_error("spectral smi: grid needs at least two samples");
  require_len(in.illuminant, n, "illuminant");
  for (int c = 0; c < 3; ++c) {
    require_len(in.cmf[static_cast<std::size_t>(c)], n, "cmf");
    require_len(in.ssf[static_cast<std::size_t>(c)], n, "ssf");
  }
  if (in.reflectance.size() < 3)
    throw std::runtime_error(
        "spectral smi: at least three test colours are required");
  for (const auto& refl : in.reflectance) require_len(refl, n, "reflectance");

  const std::vector<double> w = trapezoid_weights(in.grid_nm);

  // Perfect-diffuser white, scaled so its luminance Y = 100 (CIELAB reference).
  double xw = 0.0, yw = 0.0, zw = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double ew = in.illuminant[i] * w[i];
    xw += in.cmf[0][i] * ew;
    yw += in.cmf[1][i] * ew;
    zw += in.cmf[2][i] * ew;
  }
  if (yw <= 0.0)
    throw std::runtime_error(
        "spectral smi: illuminant x CMF gives non-positive white luminance");
  const double scale = 100.0 / yw;
  const Xyz white{xw * scale, 100.0, zw * scale};

  std::vector<Xyz> target;
  std::vector<CameraRgbPatch> camera;
  target.reserve(in.reflectance.size());
  camera.reserve(in.reflectance.size());
  for (const auto& refl : in.reflectance) {
    double x = 0.0, y = 0.0, z = 0.0, r = 0.0, g = 0.0, b = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
      const double e = in.illuminant[i] * refl[i] * w[i];
      x += in.cmf[0][i] * e;
      y += in.cmf[1][i] * e;
      z += in.cmf[2][i] * e;
      r += in.ssf[0][i] * e;
      g += in.ssf[1][i] * e;
      b += in.ssf[2][i] * e;
    }
    target.push_back(Xyz{x * scale, y * scale, z * scale});
    camera.push_back(CameraRgbPatch{r, g, b});
  }

  // Optimal (unconstrained least-squares) 3x3 RGB->XYZ transform plus its
  // residual CIELAB error, exactly the ISO 17321 characterization step.
  const CcmFit fit = fit_rgb_to_xyz_ccm(camera, target, white);

  SpectralSmiResult res;
  res.method = "iso17321_style_smi_optimal_3x3_then_mean_cielab_de";
  res.patch_count = in.reflectance.size();
  res.mean_delta_e_76 = fit.mean_delta_e_76;
  res.max_delta_e_76 = fit.max_delta_e_76;
  res.rms_delta_e_76 = fit.rms_delta_e_76;
  res.mean_delta_e_2000 = fit.mean_delta_e_2000;
  res.max_delta_e_2000 = fit.max_delta_e_2000;
  res.rms_delta_e_2000 = fit.rms_delta_e_2000;
  res.smi_slope = in.smi_slope;
  res.smi = 100.0 - in.smi_slope * fit.mean_delta_e_76;
  res.matrix = fit.matrix;
  return res;
}

}  // namespace camera_iq
