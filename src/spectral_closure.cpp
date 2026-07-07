#include "camera_iq/spectral_closure.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace camera_iq {
namespace {

constexpr double kEpsilon = 1e-12;

void validate(const SpectralClosureInputs& in) {
  const std::size_t g = in.grid_nm.size();
  if (g == 0) {
    throw std::runtime_error("spectral closure: empty wavelength grid");
  }
  for (int c = 0; c < 3; ++c) {
    if (in.ssf[static_cast<std::size_t>(c)].size() != g) {
      throw std::runtime_error("spectral closure: SSF channel size != grid");
    }
  }
  if (in.illuminant.size() != g) {
    throw std::runtime_error("spectral closure: illuminant size != grid");
  }
  const std::size_t p = in.patch_ids.size();
  if (p == 0) {
    throw std::runtime_error("spectral closure: no patches");
  }
  if (in.reflectance.size() != p || in.measured_rgb.size() != p) {
    throw std::runtime_error(
        "spectral closure: patch id / reflectance / measured count mismatch");
  }
  for (const auto& refl : in.reflectance) {
    if (refl.size() != g) {
      throw std::runtime_error("spectral closure: reflectance size != grid");
    }
  }
}

// Integral over the grid of SSF_c . E . reflectance (reflectance omitted -> 1).
std::array<double, 3> integrate(const SpectralClosureInputs& in,
                                const std::vector<double>* reflectance) {
  std::array<double, 3> out{0, 0, 0};
  for (std::size_t i = 0; i < in.grid_nm.size(); ++i) {
    const double er = in.illuminant[i] * (reflectance ? (*reflectance)[i] : 1.0);
    out[0] += in.ssf[0][i] * er;
    out[1] += in.ssf[1][i] * er;
    out[2] += in.ssf[2][i] * er;
  }
  return out;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
  const std::size_t n = a.size();
  if (n == 0) return 0.0;
  double ma = 0, mb = 0;
  for (std::size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
  ma /= static_cast<double>(n); mb /= static_cast<double>(n);
  double sab = 0, saa = 0, sbb = 0;
  for (std::size_t i = 0; i < n; ++i) {
    const double da = a[i] - ma, db = b[i] - mb;
    sab += da * db; saa += da * da; sbb += db * db;
  }
  if (saa < kEpsilon || sbb < kEpsilon) return 0.0;
  return sab / std::sqrt(saa * sbb);
}

}  // namespace

SpectralClosureResult compute_spectral_closure(const SpectralClosureInputs& in) {
  validate(in);
  SpectralClosureResult res;

  // Gate 1: white-card illuminant pairing / chromaticity check.
  const auto white_pred = integrate(in, nullptr);  // SSF . E, flat white
  if (white_pred[1] <= kEpsilon || in.white_rgb[1] <= kEpsilon) {
    throw std::runtime_error(
        "spectral closure: non-positive green in white-card gate");
  }
  res.white_ratio_predicted = {white_pred[0] / white_pred[1],
                               white_pred[2] / white_pred[1]};
  res.white_ratio_measured = {in.white_rgb[0] / in.white_rgb[1],
                              in.white_rgb[2] / in.white_rgb[1]};
  const double err_rg =
      std::abs(res.white_ratio_measured[0] - res.white_ratio_predicted[0]) /
      std::max(kEpsilon, std::abs(res.white_ratio_measured[0]));
  const double err_bg =
      std::abs(res.white_ratio_measured[1] - res.white_ratio_predicted[1]) /
      std::max(kEpsilon, std::abs(res.white_ratio_measured[1]));
  res.white_card_max_ratio_error = std::max(err_rg, err_bg);
  res.white_card_gate_passes =
      res.white_card_max_ratio_error <= in.white_gate_max_ratio_error;

  if (!res.white_card_gate_passes) {
    res.conclusion =
        "unresolved: white-card channel ratios are not consistent with the "
        "SSF-times-illuminant neutral prediction, so the capture illuminant "
        "pairing is unconfirmed; closure not computed";
    return res;
  }

  // Per-patch raw prediction and the global-scale least-squares fit.
  const std::size_t p = in.patch_ids.size();
  std::array<std::vector<double>, 3> measured, raw_pred;
  for (int c = 0; c < 3; ++c) {
    measured[static_cast<std::size_t>(c)].reserve(p);
    raw_pred[static_cast<std::size_t>(c)].reserve(p);
  }
  double num = 0, den = 0;  // global k = sum(m*pred) / sum(pred*pred)
  for (std::size_t i = 0; i < p; ++i) {
    const auto pred = integrate(in, &in.reflectance[i]);
    for (int c = 0; c < 3; ++c) {
      const double m = in.measured_rgb[i][static_cast<std::size_t>(c)];
      const double pr = pred[static_cast<std::size_t>(c)];
      measured[static_cast<std::size_t>(c)].push_back(m);
      raw_pred[static_cast<std::size_t>(c)].push_back(pr);
      num += m * pr;
      den += pr * pr;
    }
  }
  res.global_scale_k = den > kEpsilon ? num / den : 0.0;

  SpectralClosureChannel* chans[3] = {&res.r, &res.g, &res.b};
  for (int c = 0; c < 3; ++c) {
    const auto& m = measured[static_cast<std::size_t>(c)];
    const auto& pr = raw_pred[static_cast<std::size_t>(c)];
    double sumsq = 0, mean_m = 0, cnum = 0, cden = 0;
    for (std::size_t i = 0; i < p; ++i) {
      const double resid = m[i] - res.global_scale_k * pr[i];
      sumsq += resid * resid;
      mean_m += m[i];
      cnum += m[i] * pr[i];
      cden += pr[i] * pr[i];
    }
    mean_m /= static_cast<double>(p);
    const double rms = std::sqrt(sumsq / static_cast<double>(p));
    chans[c]->relative_rms =
        std::abs(mean_m) > kEpsilon ? rms / std::abs(mean_m) : 0.0;
    chans[c]->correlation = correlation(m, pr);
    chans[c]->scale_k_diagnostic = cden > kEpsilon ? cnum / cden : 0.0;
  }

  res.patches.reserve(p);
  for (std::size_t i = 0; i < p; ++i) {
    SpectralClosurePatch patch;
    patch.id = in.patch_ids[i];
    patch.measured = in.measured_rgb[i];
    for (int c = 0; c < 3; ++c) {
      patch.predicted[static_cast<std::size_t>(c)] =
          res.global_scale_k * raw_pred[static_cast<std::size_t>(c)][i];
    }
    res.patches.push_back(std::move(patch));
  }

  res.conclusion =
      "consistent with physical closure under a single global exposure scale; "
      "residuals are diagnostic tier-3 evidence, not a uniqueness proof";
  return res;
}

}  // namespace camera_iq
