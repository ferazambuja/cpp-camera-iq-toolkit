#include "camera_iq/spectral_closure.hpp"

#include <array>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::SpectralClosureInputs;
using camera_iq::compute_spectral_closure;
using test::check;
using test::check_near;

namespace {

// Synthetic set where measured == k * (SSF . E . reflectance) exactly, so a
// correct closure recovers k and reports ~0 residual. Two wavelengths, two
// patches, hand-computed:
//   raw predicted p1 = (1,0,1), p2 = (0,1,1); measured = 10 * predicted.
SpectralClosureInputs exact_synthetic(double k = 10.0) {
  SpectralClosureInputs in;
  in.grid_nm = {500, 510};
  in.ssf = {std::vector<double>{1, 0},   // R
            std::vector<double>{0, 1},   // G
            std::vector<double>{1, 1}};  // B
  in.illuminant = {1, 1};
  in.patch_ids = {"p1", "p2"};
  in.reflectance = {{1, 0}, {0, 1}};
  in.measured_rgb = {{k * 1, k * 0, k * 1}, {k * 0, k * 1, k * 1}};
  // white card reflectance flat -> raw predicted (1,1,2)
  in.white_rgb = {k * 1, k * 1, k * 2};
  return in;
}

}  // namespace

void TESTS() {
  const auto res = compute_spectral_closure(exact_synthetic(10.0));

  check(res.validation_tier == "tier3_physical_closure",
        "closure: result carries the tier-3 tier label");
  check(res.white_card_gate_passes,
        "closure: white-card gate passes on illuminant-consistent synthetic");
  check_near(res.white_card_max_ratio_error, 0.0, 1e-9,
             "closure: white-card ratio error is ~0 on exact synthetic");
  check_near(res.global_scale_k, 10.0, 1e-9,
             "closure: single global scale k is recovered");
  check(res.patches.size() == 2, "closure: every patch is reported");
  check_near(res.r.relative_rms, 0.0, 1e-9,
             "closure: R residual ~0 on exact synthetic");
  check_near(res.g.relative_rms, 0.0, 1e-9,
             "closure: G residual ~0 on exact synthetic");
  check_near(res.b.relative_rms, 0.0, 1e-9,
             "closure: B residual ~0 on exact synthetic");
  check_near(res.r.correlation, 1.0, 1e-9,
             "closure: R measured/predicted correlate on exact synthetic");

  // Gate 1 failure: corrupt the white-card ratios so they no longer match the
  // SSF-times-illuminant neutral prediction. Closure must refuse to compute.
  auto bad_white = exact_synthetic(10.0);
  bad_white.white_rgb = {50.0, 10.0, 20.0};  // R/G 5.0 vs predicted 1.0
  const auto gate_fail = compute_spectral_closure(bad_white);
  check(!gate_fail.white_card_gate_passes,
        "closure: white-card gate fails when ratios disagree");
  check(gate_fail.patches.empty(),
        "closure: no closure is computed when the gate fails");
  check(gate_fail.conclusion.find("unresolved") != std::string::npos,
        "closure: gate failure is reported as unresolved");

  // Input integrity: a reflectance/patch count mismatch must throw, not
  // silently produce a residual.
  bool threw = false;
  try {
    auto bad = exact_synthetic(10.0);
    bad.reflectance.pop_back();
    (void)compute_spectral_closure(bad);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "closure: rejects mismatched reflectance/patch counts");

  // Single global k, not per-channel: double the measured R only. One global
  // scale cannot absorb a per-channel imbalance, so the R residual must be
  // nonzero while the per-channel diagnostic k recovers the true R scale (20).
  auto imbalanced = exact_synthetic(10.0);
  imbalanced.measured_rgb = {{20, 0, 10}, {0, 10, 10}};
  const auto imb = compute_spectral_closure(imbalanced);
  check(imb.r.relative_rms > 1e-6,
        "closure: single global k leaves a real residual under channel "
        "imbalance (no per-channel fitting)");
  check_near(imb.r.scale_k_diagnostic, 20.0, 1e-9,
             "closure: per-channel R diagnostic recovers the true R scale (20)");
  check_near(imb.g.scale_k_diagnostic, 10.0, 1e-9,
             "closure: per-channel G diagnostic recovers the true G scale (10); "
             "the single global k sits between them, fitting neither exactly");
}
