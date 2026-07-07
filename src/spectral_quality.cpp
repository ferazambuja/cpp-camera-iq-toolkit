#include "camera_iq/spectral_quality.hpp"

#include <cmath>
#include <stdexcept>

namespace camera_iq {
namespace {

constexpr double kEpsilon = 1e-12;

double det3(const double m[3][3]) {
  return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
         m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
         m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

}  // namespace

SpectralQualityResult compute_spectral_quality(const SpectralQualityInputs& in) {
  const std::size_t n = in.grid_nm.size();
  if (n < 4) {
    throw std::runtime_error(
        "spectral quality: need at least 4 samples to overdetermine a 3-param "
        "fit");
  }
  for (int c = 0; c < 3; ++c) {
    if (in.ssf[static_cast<std::size_t>(c)].size() != n ||
        in.cmf[static_cast<std::size_t>(c)].size() != n) {
      throw std::runtime_error("spectral quality: SSF/CMF size != grid");
    }
  }

  // Gram matrix G = A^T A with A = [R | G | B]; b_m = A^T cmf_m.
  double gram[3][3] = {{0}};
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double s = 0;
      for (std::size_t k = 0; k < n; ++k) {
        s += in.ssf[static_cast<std::size_t>(i)][k] *
             in.ssf[static_cast<std::size_t>(j)][k];
      }
      gram[i][j] = s;
    }
  }
  const double det = det3(gram);
  const double diag_product = gram[0][0] * gram[1][1] * gram[2][2];
  // Hadamard: for a Gram matrix 0 <= det <= product(diagonals). A ratio near 0
  // means the SSF channels are (near-)linearly dependent -> no unique fit.
  if (diag_product <= kEpsilon || std::abs(det) <= 1e-9 * diag_product) {
    throw std::runtime_error(
        "spectral quality: rank-deficient SSF basis (channels not independent)");
  }

  SpectralQualityResult res;
  double sum_sq = 0;
  for (int m = 0; m < 3; ++m) {
    double b[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
      for (std::size_t k = 0; k < n; ++k) {
        b[i] += in.ssf[static_cast<std::size_t>(i)][k] *
                in.cmf[static_cast<std::size_t>(m)][k];
      }
    }
    // Solve gram * coef = b by Cramer's rule.
    double coef[3];
    for (int col = 0; col < 3; ++col) {
      double mc[3][3];
      for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) mc[i][j] = (j == col) ? b[i] : gram[i][j];
      coef[col] = det3(mc) / det;
    }
    double num = 0, den = 0;
    for (std::size_t k = 0; k < n; ++k) {
      const double fit = coef[0] * in.ssf[0][k] + coef[1] * in.ssf[1][k] +
                         coef[2] * in.ssf[2][k];
      const double d = in.cmf[static_cast<std::size_t>(m)][k] - fit;
      num += d * d;
      den += in.cmf[static_cast<std::size_t>(m)][k] *
             in.cmf[static_cast<std::size_t>(m)][k];
    }
    res.cmf_residual[static_cast<std::size_t>(m)] =
        den > kEpsilon ? std::sqrt(num / den) : 0.0;
    sum_sq += res.cmf_residual[static_cast<std::size_t>(m)] *
              res.cmf_residual[static_cast<std::size_t>(m)];
  }
  res.combined_residual = std::sqrt(sum_sq / 3.0);
  res.quality_index = 1.0 - res.combined_residual;
  return res;
}

}  // namespace camera_iq
