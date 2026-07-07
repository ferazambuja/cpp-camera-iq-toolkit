#include "camera_iq/spectral_smi.hpp"

#include <array>
#include <cmath>
#include <vector>

#include "harness.hpp"

using camera_iq::compute_spectral_smi;
using camera_iq::SpectralSmiInputs;
using test::check;
using test::check_near;

namespace {

// A smooth, strictly-positive synthetic "CMF": three offset Gaussians that are
// linearly independent. The SMI math is agnostic to whether these are the real
// CIE curves; the tests exercise the metric, not colorimetric ground truth.
double gauss(double x, double mu, double sigma) {
  const double d = (x - mu) / sigma;
  return std::exp(-0.5 * d * d);
}

SpectralSmiInputs base_inputs() {
  SpectralSmiInputs in;
  for (int wl = 380; wl <= 730; wl += 10) in.grid_nm.push_back(wl);
  const std::size_t n = in.grid_nm.size();

  in.illuminant.assign(n, 0.0);
  for (std::size_t i = 0; i < n; ++i) {
    // Gently sloped daylight-ish illuminant, all positive.
    in.illuminant[i] = 80.0 + 0.05 * (in.grid_nm[i] - 380.0);
  }

  for (std::size_t i = 0; i < n; ++i) {
    const double w = in.grid_nm[i];
    in.cmf[0].push_back(1.0 * gauss(w, 600, 40) + 0.35 * gauss(w, 445, 20));
    in.cmf[1].push_back(1.0 * gauss(w, 555, 45));
    in.cmf[2].push_back(1.7 * gauss(w, 445, 22));
  }

  // Six test colours with distinct spectral shapes so a metameric SSF cannot be
  // linearly corrected away.
  const std::array<std::array<double, 3>, 6> shape = {{
      {600, 60, 0.8}, {450, 50, 0.7}, {550, 40, 0.9},
      {500, 120, 0.5}, {680, 55, 0.85}, {520, 200, 0.6}}};
  for (const auto& s : shape) {
    std::vector<double> refl(n, 0.0);
    for (std::size_t i = 0; i < n; ++i)
      refl[i] = 0.05 + s[2] * gauss(in.grid_nm[i], s[0], s[1]);
    in.reflectance.push_back(refl);
  }
  return in;
}

}  // namespace

void TESTS() {
  // 1. Luther-satisfying camera: SSF == CMF. A single scaled-identity 3x3 maps
  //    camera RGB onto true XYZ exactly, so the residual is ~0 and SMI ~ 100.
  {
    SpectralSmiInputs in = base_inputs();
    in.ssf = in.cmf;
    const auto r = compute_spectral_smi(in);
    check(r.patch_count == 6, "smi: patch count reflects test colours");
    check_near(r.mean_delta_e_76, 0.0, 0.05,
               "smi: Luther camera has ~zero mean CIELAB error");
    check_near(r.smi, 100.0, 0.5, "smi: Luther camera scores ~100");
    check_near(r.white_preserving_mean_delta_e_76, 0.0, 0.05,
               "smi: white-preserving Luther fit has ~zero mean error");
    check_near(r.white_preserving_smi, 100.0, 0.5,
               "smi: white-preserving Luther fit scores ~100");
    check_near(r.white_preserving_white_delta_e_76, 0.0, 1e-9,
               "smi: white-preserving fit maps camera white to XYZ white");
  }

  // 2. Metameric camera: the SSF peaks are wavelength-shifted off the CMF peaks.
  //    A shifted Gaussian is not a linear combination of the unshifted basis, so
  //    no 3x3 corrects it and the residual error is real (a camera whose CFA
  //    peaks sit away from the CMF peaks is the realistic failure mode).
  {
    SpectralSmiInputs in = base_inputs();
    for (std::size_t i = 0; i < in.grid_nm.size(); ++i) {
      const double w = in.grid_nm[i];
      in.ssf[0].push_back(1.0 * gauss(w, 625, 40) + 0.35 * gauss(w, 470, 20));
      in.ssf[1].push_back(1.0 * gauss(w, 580, 45));
      in.ssf[2].push_back(1.7 * gauss(w, 470, 22));
    }
    const auto r = compute_spectral_smi(in);
    check(r.mean_delta_e_76 > 0.1,
          "smi: metameric camera has non-trivial mean CIELAB error");
    check(r.smi < 100.0, "smi: metameric camera scores below 100");
  }

  // 3. Reported SMI is exactly 100 - slope * mean CIELAB(1976) error.
  {
    SpectralSmiInputs in = base_inputs();
    in.ssf = in.cmf;
    for (std::size_t i = 0; i < in.grid_nm.size(); ++i) in.ssf[2][i] = 1.0;
    in.smi_slope = 5.5;
    const auto r = compute_spectral_smi(in);
    check_near(r.smi, 100.0 - 5.5 * r.mean_delta_e_76, 1e-9,
               "smi: score matches 100 - slope * meanDE76");
    check_near(r.white_preserving_smi,
               100.0 - 5.5 * r.white_preserving_mean_delta_e_76, 1e-9,
               "smi: white-preserving score matches 100 - slope * meanDE76");
    check(std::abs(r.white_preserving_delta_smi) > 1e-6,
          "smi: white-preserving sensitivity is not a duplicate of default fit");
    check_near(r.white_preserving_white_delta_e_76, 0.0, 1e-9,
               "smi: white-preserving non-Luther fit maps camera white to XYZ white");
    check_near(r.smi_slope, 5.5, 1e-12, "smi: slope echoed in result");
  }

  // 4. Grid / size mismatch is rejected.
  {
    SpectralSmiInputs in = base_inputs();
    in.ssf = in.cmf;
    in.ssf[0].pop_back();
    bool threw = false;
    try {
      compute_spectral_smi(in);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "smi: SSF length mismatch throws");
  }

  // 5. Fewer than three test colours is rejected (3x3 fit is underdetermined).
  {
    SpectralSmiInputs in = base_inputs();
    in.ssf = in.cmf;
    in.reflectance.resize(2);
    bool threw = false;
    try {
      compute_spectral_smi(in);
    } catch (const std::exception&) {
      threw = true;
    }
    check(threw, "smi: fewer than three test colours throws");
  }
}
