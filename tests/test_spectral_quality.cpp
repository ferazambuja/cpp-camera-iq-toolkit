#include "camera_iq/spectral_quality.hpp"

#include <array>
#include <cmath>
#include <vector>

#include "harness.hpp"

using camera_iq::SpectralQualityInputs;
using camera_iq::compute_spectral_quality;
using test::check;
using test::check_near;

namespace {

// 5-wavelength basis so the 3-parameter fit is overdetermined (residual is
// meaningful). SSF are three unit axes; a CMF equal to an SSF (or their sum)
// fits exactly, a CMF on an unused axis cannot be fit at all.
SpectralQualityInputs synthetic() {
  SpectralQualityInputs in;
  in.grid_nm = {500, 510, 520, 530, 540};
  in.ssf = {std::vector<double>{1, 0, 0, 0, 0},   // R
            std::vector<double>{0, 1, 0, 0, 0},   // G
            std::vector<double>{0, 0, 1, 0, 0}};  // B
  in.cmf = {std::vector<double>{1, 0, 0, 0, 0},   // xbar == R -> residual 0
            std::vector<double>{0, 0, 0, 1, 0},   // ybar on unused axis -> res 1
            std::vector<double>{1, 1, 0, 0, 0}};  // zbar == R+G -> residual 0
  return in;
}

}  // namespace

void TESTS() {
  const auto res = compute_spectral_quality(synthetic());

  check_near(res.cmf_residual[0], 0.0, 1e-9,
             "quality: CMF equal to an SSF channel fits exactly");
  check_near(res.cmf_residual[1], 1.0, 1e-9,
             "quality: CMF on an axis the SSF cannot reach has residual 1");
  check_near(res.cmf_residual[2], 0.0, 1e-9,
             "quality: CMF equal to a linear combination of SSFs fits exactly");
  check_near(res.combined_residual, std::sqrt(1.0 / 3.0), 1e-9,
             "quality: combined residual is the RMS over the three CMF fits");
  check_near(res.quality_index, 1.0 - std::sqrt(1.0 / 3.0), 1e-9,
             "quality: index is 1 minus the combined residual");

  // Degenerate SSF basis (two identical channels + a dependent third) must not
  // silently return garbage; a rank-deficient basis is rejected.
  SpectralQualityInputs deg = synthetic();
  deg.ssf[1] = deg.ssf[0];
  deg.ssf[2] = deg.ssf[0];
  bool threw = false;
  try {
    (void)compute_spectral_quality(deg);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "quality: rejects a rank-deficient SSF basis");

  // Grid/size mismatch throws.
  SpectralQualityInputs bad = synthetic();
  bad.cmf[0].pop_back();
  bool threw2 = false;
  try {
    (void)compute_spectral_quality(bad);
  } catch (const std::runtime_error&) {
    threw2 = true;
  }
  check(threw2, "quality: rejects grid/size mismatch");
}
