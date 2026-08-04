#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "camera_iq/spectral_quality.hpp"
#include "harness.hpp"

using camera_iq::compute_spectral_quality;
using camera_iq::SpectralQualityInputs;
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
            std::vector<double>{0, 0, 0, 1, 0},  // ybar on unused axis -> res 1
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

  // DOC-EVIDENCE: spectral-fidelity.luther-scale-invariance
  // The Luther residual depends on the subspace spanned by the three SSFs,
  // not on their arbitrary amplitude normalization. A small common scale used
  // to trip the absolute Gram-matrix threshold even though the basis remained
  // full rank.
  SpectralQualityInputs small = synthetic();
  for (auto& channel : small.ssf) {
    for (double& value : channel) value *= 1e-8;
  }
  const auto small_res = compute_spectral_quality(small);
  for (std::size_t i = 0; i < 3; ++i) {
    check_near(small_res.cmf_residual[i], res.cmf_residual[i], 1e-12,
               "quality: common SSF scaling preserves each CMF residual");
  }
  check_near(small_res.combined_residual, res.combined_residual, 1e-12,
             "quality: common SSF scaling preserves the combined residual");

  SpectralQualityInputs per_channel = synthetic();
  constexpr std::array<double, 3> scale{1e-6, 1e3, 7.0};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    for (double& value : per_channel.ssf[channel]) value *= scale[channel];
  }
  const auto per_channel_res = compute_spectral_quality(per_channel);
  check_near(per_channel_res.combined_residual, res.combined_residual, 1e-12,
             "quality: independent SSF channel scaling preserves the residual");

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

  SpectralQualityInputs nonfinite_ssf = synthetic();
  nonfinite_ssf.ssf[1][2] = std::numeric_limits<double>::infinity();
  bool threw3 = false;
  try {
    (void)compute_spectral_quality(nonfinite_ssf);
  } catch (const std::runtime_error&) {
    threw3 = true;
  }
  check(threw3, "quality: rejects non-finite SSF samples");

  SpectralQualityInputs nonfinite_cmf = synthetic();
  nonfinite_cmf.cmf[2][4] = std::numeric_limits<double>::quiet_NaN();
  bool threw4 = false;
  try {
    (void)compute_spectral_quality(nonfinite_cmf);
  } catch (const std::runtime_error&) {
    threw4 = true;
  }
  check(threw4, "quality: rejects non-finite CMF samples");

  SpectralQualityInputs zero_cmf = synthetic();
  zero_cmf.cmf[0].assign(zero_cmf.grid_nm.size(), 0.0);
  bool threw5 = false;
  try {
    (void)compute_spectral_quality(zero_cmf);
  } catch (const std::runtime_error&) {
    threw5 = true;
  }
  check(threw5, "quality: rejects a zero-norm CMF target");
}
