#include "camera_iq/localization_diagnosis.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace camera_iq {
namespace {

constexpr int kRows = 10;
constexpr int kColumns = 14;
constexpr double kEpsilon = 1e-12;

struct Sample {
  PatchCenterResidual residual;
  double u = 0;
  double v = 0;
  double nx = 0;
  double ny = 0;
};

struct Prediction {
  double dx = 0;
  double dy = 0;
};

struct FittedModel {
  std::string name;
  std::string hypothesis;
  int dof = 0;
  bool production_candidate = false;
  std::function<Prediction(const Sample&)> predict;
};

using FoldSelector = std::function<int(const Sample&)>;

bool finite(double value) { return std::isfinite(value); }

double safe_ratio(double numerator, double denominator) {
  if (denominator <= kEpsilon) {
    return numerator <= kEpsilon ? 0.0
                                 : std::numeric_limits<double>::infinity();
  }
  return numerator / denominator;
}

double vector_length(double x, double y) { return std::sqrt(x * x + y * y); }

double patch_center_x(const PatchCoord& coord) {
  return coord.x - 1.0 + coord.width / 2.0;
}

double patch_center_y(const PatchCoord& coord) {
  return coord.y - 1.0 + coord.height / 2.0;
}

std::vector<Sample> make_samples(
    const std::vector<PatchCenterResidual>& residuals, Point2d stated_center) {
  if (residuals.size() < static_cast<std::size_t>(kRows * kColumns)) {
    throw std::runtime_error(
        "localization diagnosis requires a complete 140-patch residual set");
  }
  double max_abs_x = 0;
  double max_abs_y = 0;
  for (const auto& r : residuals) {
    if (!finite(r.generated_center_x) || !finite(r.generated_center_y) ||
        !finite(r.dx_px) || !finite(r.dy_px)) {
      throw std::runtime_error(
          "localization diagnosis requires finite residual values");
    }
    max_abs_x =
        std::max(max_abs_x, std::abs(r.generated_center_x - stated_center.x));
    max_abs_y =
        std::max(max_abs_y, std::abs(r.generated_center_y - stated_center.y));
  }
  const double radial_scale = std::max(max_abs_x, max_abs_y);
  if (radial_scale <= 0) {
    throw std::runtime_error(
        "localization diagnosis requires a non-degenerate center basis");
  }

  std::vector<Sample> samples;
  samples.reserve(residuals.size());
  for (const auto& r : residuals) {
    Sample s;
    s.residual = r;
    s.u = (static_cast<double>(r.column) - 6.5) / 6.5;
    s.v = (static_cast<double>(r.row) - 4.5) / 4.5;
    // Use one common image-space scale. Per-axis normalization would erase the
    // wide SG chart geometry and incorrectly force radial dx/dy anisotropy
    // toward one, which is exactly the false discriminator this slice avoids.
    s.nx = (r.generated_center_x - stated_center.x) / radial_scale;
    s.ny = (r.generated_center_y - stated_center.y) / radial_scale;
    samples.push_back(s);
  }
  return samples;
}

double worst_heldout_rms(const LocalizationModelReport& model) {
  double worst = 0;
  for (const auto& score : model.heldout_scores) {
    worst = std::max(worst, score.metrics.rms_px);
  }
  return worst;
}

std::vector<double> solve_linear_system(std::vector<std::vector<double>> a,
                                        std::vector<double> b) {
  const std::size_t n = b.size();
  if (a.size() != n) {
    throw std::runtime_error("linear solve: invalid matrix size");
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (a[i].size() != n) {
      throw std::runtime_error("linear solve: invalid row size");
    }
  }
  for (std::size_t col = 0; col < n; ++col) {
    std::size_t pivot = col;
    double pivot_abs = std::abs(a[col][col]);
    for (std::size_t row = col + 1; row < n; ++row) {
      const double candidate = std::abs(a[row][col]);
      if (candidate > pivot_abs) {
        pivot = row;
        pivot_abs = candidate;
      }
    }
    if (pivot_abs < kEpsilon) {
      throw std::runtime_error("linear solve: singular model fit");
    }
    if (pivot != col) {
      std::swap(a[pivot], a[col]);
      std::swap(b[pivot], b[col]);
    }

    const double scale = a[col][col];
    for (std::size_t k = col; k < n; ++k) {
      a[col][k] /= scale;
    }
    b[col] /= scale;

    for (std::size_t row = 0; row < n; ++row) {
      if (row == col) continue;
      const double factor = a[row][col];
      for (std::size_t k = col; k < n; ++k) {
        a[row][k] -= factor * a[col][k];
      }
      b[row] -= factor * b[col];
    }
  }
  return b;
}

using BasisFn = std::function<std::vector<double>(const Sample&, int component)>;

std::vector<double> least_squares_fit(const std::vector<Sample>& samples,
                                      const std::vector<bool>& train,
                                      std::size_t param_count,
                                      const BasisFn& basis) {
  std::vector<std::vector<double>> ata(
      param_count, std::vector<double>(param_count, 0.0));
  std::vector<double> atb(param_count, 0.0);
  std::size_t rows = 0;
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (!train[i]) continue;
    for (int component = 0; component < 2; ++component) {
      const auto row = basis(samples[i], component);
      if (row.size() != param_count) {
        throw std::runtime_error("localization diagnosis: invalid basis size");
      }
      const double target =
          component == 0 ? samples[i].residual.dx_px
                         : samples[i].residual.dy_px;
      for (std::size_t r = 0; r < param_count; ++r) {
        atb[r] += row[r] * target;
        for (std::size_t c = 0; c < param_count; ++c) {
          ata[r][c] += row[r] * row[c];
        }
      }
      ++rows;
    }
  }
  if (rows < param_count) {
    throw std::runtime_error("localization diagnosis: not enough fit samples");
  }
  return solve_linear_system(ata, atb);
}

FittedModel fit_linear_model(const std::vector<Sample>& samples,
                             const std::vector<bool>& train, std::string name,
                             std::string hypothesis, int dof,
                             const BasisFn& basis) {
  const auto params = least_squares_fit(samples, train,
                                        static_cast<std::size_t>(dof), basis);
  return {std::move(name),
          std::move(hypothesis),
          dof,
          false,
          [params, basis](const Sample& s) {
            Prediction p;
            for (int component = 0; component < 2; ++component) {
              const auto row = basis(s, component);
              double value = 0;
              for (std::size_t i = 0; i < params.size(); ++i) {
                value += row[i] * params[i];
              }
              if (component == 0) {
                p.dx = value;
              } else {
                p.dy = value;
              }
            }
            return p;
          }};
}

FittedModel fit_zero_model() {
  return {"corner_seeded_homography_baseline",
          "baseline: no correction to the current corner-seeded homography",
          0,
          false,
          [](const Sample&) { return Prediction{}; }};
}

FittedModel fit_affine_model(const std::vector<Sample>& samples,
                             const std::vector<bool>& train) {
  return fit_linear_model(
      samples, train, "affine_linear_pitch_baseline",
      "baseline: linear pitch/scale/shift residual field", 6,
      [](const Sample& s, int component) {
        std::vector<double> row(6, 0.0);
        const std::size_t offset = component == 0 ? 0 : 3;
        row[offset] = 1.0;
        row[offset + 1] = s.u;
        row[offset + 2] = s.v;
        return row;
      });
}

FittedModel fit_radial_model(const std::vector<Sample>& samples,
                             const std::vector<bool>& train) {
  return fit_linear_model(
      samples, train, "isotropic_radial_k1_baseline",
      "baseline: one-parameter isotropic radial field about the stated centre",
      1, [](const Sample& s, int component) {
        const double r2 = s.nx * s.nx + s.ny * s.ny;
        return std::vector<double>{component == 0 ? s.nx * r2 : s.ny * r2};
      });
}

FittedModel fit_radial_k2_model(const std::vector<Sample>& samples,
                                const std::vector<bool>& train) {
  return fit_linear_model(
      samples, train, "isotropic_radial_k1_k2_baseline",
      "baseline: two-parameter isotropic radial field about the stated centre",
      2, [](const Sample& s, int component) {
        const double r2 = s.nx * s.nx + s.ny * s.ny;
        const double axis = component == 0 ? s.nx : s.ny;
        return std::vector<double>{axis * r2, axis * r2 * r2};
      });
}

FittedModel fit_tangential_model(const std::vector<Sample>& samples,
                                 const std::vector<bool>& train) {
  return fit_linear_model(
      samples, train, "tangential_decentering_candidate",
      "candidate: two-parameter tangential/decentering distortion field", 2,
      [](const Sample& s, int component) {
        const double r2 = s.nx * s.nx + s.ny * s.ny;
        if (component == 0) {
          return std::vector<double>{2.0 * s.nx * s.ny,
                                     r2 + 2.0 * s.nx * s.nx};
        }
        return std::vector<double>{r2 + 2.0 * s.ny * s.ny,
                                   2.0 * s.nx * s.ny};
      });
}

FittedModel fit_cylindrical_model(const std::vector<Sample>& samples,
                                  const std::vector<bool>& train) {
  return fit_linear_model(
      samples, train, "chart_cylindrical_bow_candidate",
      "candidate: chart-space second-order horizontal bow", 3,
      [](const Sample& s, int component) {
        const double q = 1.0 - s.u * s.u;
        if (component == 0) {
          return std::vector<double>{q, q * s.v, 0.0};
        }
        return std::vector<double>{0.0, 0.0, q};
      });
}

FittedModel fit_polynomial_model(const std::vector<Sample>& samples,
                                 const std::vector<bool>& train) {
  return fit_linear_model(
      samples, train, "smooth_polynomial_degree2_warp_candidate",
      "candidate: fixed degree-2 bivariate smooth placement warp", 12,
      [](const Sample& s, int component) {
        std::vector<double> row(12, 0.0);
        const std::size_t offset = component == 0 ? 0 : 6;
        row[offset] = 1.0;
        row[offset + 1] = s.u;
        row[offset + 2] = s.v;
        row[offset + 3] = s.u * s.u;
        row[offset + 4] = s.u * s.v;
        row[offset + 5] = s.v * s.v;
        return row;
      });
}

FittedModel fit_homography_model(const std::vector<Sample>& samples,
                                 const std::vector<bool>& train) {
  double min_x = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  for (const auto& s : samples) {
    min_x = std::min(min_x, s.residual.generated_center_x);
    max_x = std::max(max_x, s.residual.generated_center_x);
    min_y = std::min(min_y, s.residual.generated_center_y);
    max_y = std::max(max_y, s.residual.generated_center_y);
  }
  const double cx = (min_x + max_x) / 2.0;
  const double cy = (min_y + max_y) / 2.0;
  const double sx = (max_x - min_x) / 2.0;
  const double sy = (max_y - min_y) / 2.0;
  if (sx <= 0 || sy <= 0) {
    throw std::runtime_error("localization diagnosis: degenerate homography");
  }

  auto norm_x = [=](double x) { return (x - cx) / sx; };
  auto norm_y = [=](double y) { return (y - cy) / sy; };
  auto denorm_x = [=](double x) { return x * sx + cx; };
  auto denorm_y = [=](double y) { return y * sy + cy; };

  auto basis = [&](const Sample& s, int component) {
    const double x = norm_x(s.residual.generated_center_x);
    const double y = norm_y(s.residual.generated_center_y);
    const double ox =
        norm_x(s.residual.generated_center_x - s.residual.dx_px);
    const double oy =
        norm_y(s.residual.generated_center_y - s.residual.dy_px);
    std::vector<double> row(8, 0.0);
    if (component == 0) {
      row = {x, y, 1.0, 0.0, 0.0, 0.0, -ox * x, -ox * y};
    } else {
      row = {0.0, 0.0, 0.0, x, y, 1.0, -oy * x, -oy * y};
    }
    return row;
  };

  std::vector<std::vector<double>> ata(8, std::vector<double>(8, 0.0));
  std::vector<double> atb(8, 0.0);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (!train[i]) continue;
    for (int component = 0; component < 2; ++component) {
      const auto row = basis(samples[i], component);
      const double target =
          component == 0
              ? norm_x(samples[i].residual.generated_center_x -
                       samples[i].residual.dx_px)
              : norm_y(samples[i].residual.generated_center_y -
                       samples[i].residual.dy_px);
      for (std::size_t r = 0; r < 8; ++r) {
        atb[r] += row[r] * target;
        for (std::size_t c = 0; c < 8; ++c) {
          ata[r][c] += row[r] * row[c];
        }
      }
    }
  }
  const auto h = solve_linear_system(ata, atb);
  return {"all_center_lsq_homography_ceiling",
          "ceiling: all-center least-squares homography, not production",
          8,
          false,
          [h, norm_x, norm_y, denorm_x, denorm_y](const Sample& s) {
            const double x = norm_x(s.residual.generated_center_x);
            const double y = norm_y(s.residual.generated_center_y);
            const double denom = h[6] * x + h[7] * y + 1.0;
            if (std::abs(denom) < kEpsilon) {
              return Prediction{};
            }
            const double px = (h[0] * x + h[1] * y + h[2]) / denom;
            const double py = (h[3] * x + h[4] * y + h[5]) / denom;
            return Prediction{s.residual.generated_center_x - denorm_x(px),
                              s.residual.generated_center_y - denorm_y(py)};
          }};
}

std::vector<bool> all_train(std::size_t count) {
  return std::vector<bool>(count, true);
}

std::vector<FittedModel> fit_all_models(const std::vector<Sample>& samples,
                                        const std::vector<bool>& train) {
  return {fit_zero_model(),
          fit_homography_model(samples, train),
          fit_radial_model(samples, train),
          fit_radial_k2_model(samples, train),
          fit_affine_model(samples, train),
          fit_tangential_model(samples, train),
          fit_cylindrical_model(samples, train),
          fit_polynomial_model(samples, train)};
}

LocalizationMetricSummary summarize_residuals(
    const std::vector<Sample>& samples, const std::vector<bool>& selected,
    const std::function<Prediction(const Sample&)>& predict) {
  double sumsq = 0;
  double max_distance = 0;
  double dx_sumsq = 0;
  double dy_sumsq = 0;
  std::size_t count = 0;
  std::vector<Prediction> remaining(samples.size());
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const auto p = predict(samples[i]);
    remaining[i] = {samples[i].residual.dx_px - p.dx,
                    samples[i].residual.dy_px - p.dy};
    if (!selected[i]) continue;
    const double dx = remaining[i].dx;
    const double dy = remaining[i].dy;
    const double dist = vector_length(dx, dy);
    sumsq += dist * dist;
    dx_sumsq += dx * dx;
    dy_sumsq += dy * dy;
    max_distance = std::max(max_distance, dist);
    ++count;
  }
  if (count == 0) {
    throw std::runtime_error("localization diagnosis: empty metric set");
  }

  double cosine_sum = 0;
  std::size_t cosine_count = 0;
  auto add_cosine = [&](std::size_t a, std::size_t b) {
    if (!selected[a] || !selected[b]) return;
    const double la = vector_length(remaining[a].dx, remaining[a].dy);
    const double lb = vector_length(remaining[b].dx, remaining[b].dy);
    if (la <= kEpsilon || lb <= kEpsilon) return;
    cosine_sum +=
        (remaining[a].dx * remaining[b].dx + remaining[a].dy * remaining[b].dy) /
        (la * lb);
    ++cosine_count;
  };
  for (std::size_t i = 0; i < samples.size(); ++i) {
    const int row = samples[i].residual.row;
    const int column = samples[i].residual.column;
    if (column + 1 < kColumns) {
      add_cosine(i, i + 1);
    }
    if (row + 1 < kRows) {
      add_cosine(i, i + kColumns);
    }
  }

  LocalizationMetricSummary metrics;
  metrics.sample_count = count;
  metrics.rms_px = std::sqrt(sumsq / static_cast<double>(count));
  metrics.max_px = max_distance;
  metrics.dx_rms_px = std::sqrt(dx_sumsq / static_cast<double>(count));
  metrics.dy_rms_px = std::sqrt(dy_sumsq / static_cast<double>(count));
  metrics.dx_dy_anisotropy = safe_ratio(metrics.dx_rms_px, metrics.dy_rms_px);
  metrics.adjacent_vector_cosine =
      cosine_count > 0 ? cosine_sum / static_cast<double>(cosine_count) : 0.0;
  return metrics;
}

LocalizationMetricSummary summarize_all(
    const std::vector<Sample>& samples,
    const std::function<Prediction(const Sample&)>& predict) {
  return summarize_residuals(samples, all_train(samples.size()), predict);
}

LocalizationHoldoutScore heldout_score(
    const std::vector<Sample>& samples, const std::string& split, int folds,
    const FoldSelector& fold_selector,
    const std::function<FittedModel(const std::vector<Sample>&,
                                    const std::vector<bool>&)>& fitter) {
  double sumsq = 0;
  double max_distance = 0;
  double dx_sumsq = 0;
  double dy_sumsq = 0;
  std::size_t total = 0;
  double cosine_weighted = 0;
  for (int fold = 0; fold < folds; ++fold) {
    std::vector<bool> train(samples.size(), true);
    std::vector<bool> test(samples.size(), false);
    for (std::size_t i = 0; i < samples.size(); ++i) {
      if (fold_selector(samples[i]) == fold) {
        train[i] = false;
        test[i] = true;
      }
    }
    const auto model = fitter(samples, train);
    const auto metrics = summarize_residuals(samples, test, model.predict);
    sumsq += metrics.rms_px * metrics.rms_px *
             static_cast<double>(metrics.sample_count);
    max_distance = std::max(max_distance, metrics.max_px);
    dx_sumsq += metrics.dx_rms_px * metrics.dx_rms_px *
                static_cast<double>(metrics.sample_count);
    dy_sumsq += metrics.dy_rms_px * metrics.dy_rms_px *
                static_cast<double>(metrics.sample_count);
    cosine_weighted += metrics.adjacent_vector_cosine *
                       static_cast<double>(metrics.sample_count);
    total += metrics.sample_count;
  }
  if (total == 0) {
    throw std::runtime_error("localization diagnosis: empty held-out split");
  }
  LocalizationMetricSummary metrics;
  metrics.sample_count = total;
  metrics.rms_px = std::sqrt(sumsq / static_cast<double>(total));
  metrics.max_px = max_distance;
  metrics.dx_dy_anisotropy =
      safe_ratio(std::sqrt(dx_sumsq / static_cast<double>(total)),
                 std::sqrt(dy_sumsq / static_cast<double>(total)));
  metrics.dx_rms_px = std::sqrt(dx_sumsq / static_cast<double>(total));
  metrics.dy_rms_px = std::sqrt(dy_sumsq / static_cast<double>(total));
  metrics.adjacent_vector_cosine =
      cosine_weighted / static_cast<double>(total);
  return {split, static_cast<std::size_t>(folds), metrics};
}

std::vector<LocalizationHoldoutScore> heldout_scores_for_model(
    const std::vector<Sample>& samples,
    const std::function<FittedModel(const std::vector<Sample>&,
                                    const std::vector<bool>&)>& fitter) {
  return {
      heldout_score(samples, "checkerboard", 2,
                    [](const Sample& s) {
                      return (s.residual.row + s.residual.column) & 1;
                    },
                    fitter),
      heldout_score(samples, "row_block", 5,
                    [](const Sample& s) { return s.residual.row / 2; },
                    fitter),
      heldout_score(samples, "column_block", 7,
                    [](const Sample& s) { return s.residual.column / 2; },
                    fitter),
  };
}

double radial_basis_anisotropy(const std::vector<Sample>& samples) {
  double bx_sumsq = 0;
  double by_sumsq = 0;
  for (const auto& s : samples) {
    const double r2 = s.nx * s.nx + s.ny * s.ny;
    bx_sumsq += s.nx * r2 * s.nx * r2;
    by_sumsq += s.ny * r2 * s.ny * r2;
  }
  return safe_ratio(std::sqrt(bx_sumsq / static_cast<double>(samples.size())),
                    std::sqrt(by_sumsq / static_cast<double>(samples.size())));
}

}  // namespace

LocalizationModelComparison analyze_localization_residual_models(
    const std::vector<PatchCenterResidual>& residuals, Point2d stated_center) {
  if (!finite(stated_center.x) || !finite(stated_center.y)) {
    throw std::runtime_error(
        "localization diagnosis requires a finite stated centre");
  }
  const auto samples = make_samples(residuals, stated_center);

  LocalizationModelComparison comparison;
  comparison.patch_count = samples.size();
  comparison.identifiability_note =
      "near-centred capture can leave lens distortion and chart geometry "
      "confounded; if held-out residuals tie within the measurement floor, "
      "report unresolved and use an off-centre or multi-position capture";
  comparison.observed_anisotropy_dx_over_dy =
      summarize_all(samples, [](const Sample&) {
        return Prediction{0.0, 0.0};
      }).dx_dy_anisotropy;
  comparison.isotropic_radial_predicted_anisotropy_dx_over_dy =
      radial_basis_anisotropy(samples);

  const auto train_all = all_train(samples.size());
  const auto full_models = fit_all_models(samples, train_all);
  for (const auto& full_model : full_models) {
    auto fitter = [&](const std::vector<Sample>& fit_samples,
                      const std::vector<bool>& train) {
      if (full_model.name == "corner_seeded_homography_baseline") {
        return fit_zero_model();
      }
      if (full_model.name == "all_center_lsq_homography_ceiling") {
        return fit_homography_model(fit_samples, train);
      }
      if (full_model.name == "isotropic_radial_k1_baseline") {
        return fit_radial_model(fit_samples, train);
      }
      if (full_model.name == "isotropic_radial_k1_k2_baseline") {
        return fit_radial_k2_model(fit_samples, train);
      }
      if (full_model.name == "affine_linear_pitch_baseline") {
        return fit_affine_model(fit_samples, train);
      }
      if (full_model.name == "tangential_decentering_candidate") {
        return fit_tangential_model(fit_samples, train);
      }
      if (full_model.name == "chart_cylindrical_bow_candidate") {
        return fit_cylindrical_model(fit_samples, train);
      }
      return fit_polynomial_model(fit_samples, train);
    };

    LocalizationModelReport report;
    report.name = full_model.name;
    report.hypothesis = full_model.hypothesis;
    report.degrees_of_freedom = full_model.dof;
    report.production_candidate = false;
    report.in_sample = summarize_all(samples, full_model.predict);
    report.heldout_scores = heldout_scores_for_model(samples, fitter);
    comparison.models.push_back(report);
  }

  const bool has_radial = std::any_of(
      comparison.models.begin(), comparison.models.end(), [](const auto& m) {
        return m.name == "isotropic_radial_k1_baseline";
      });
  const bool has_affine = std::any_of(
      comparison.models.begin(), comparison.models.end(), [](const auto& m) {
        return m.name == "affine_linear_pitch_baseline";
      });
  comparison.radial_affine_baselines_reported = has_radial && has_affine;
  return comparison;
}

std::vector<IndependentPatchCenter> estimate_patch_centers_by_color_centroid(
    const std::vector<RgbPixel>& image, int width, int height,
    const std::vector<PatchCoord>& coords, double search_scale) {
  if (width <= 0 || height <= 0 ||
      image.size() != static_cast<std::size_t>(width * height)) {
    throw std::runtime_error(
        "independent center: image dimensions do not match pixel buffer");
  }
  if (!std::isfinite(search_scale) || search_scale <= 0) {
    throw std::runtime_error(
        "independent center: search scale must be positive and finite");
  }

  std::vector<IndependentPatchCenter> out;
  out.reserve(coords.size());
  for (const auto& coord : coords) {
    const double cx = patch_center_x(coord);
    const double cy = patch_center_y(coord);
    const double search_half_x = std::max(coord.width * search_scale, 4.0);
    const double search_half_y = std::max(coord.height * search_scale, 4.0);

    const int inner_left =
        std::max(0, static_cast<int>(std::floor(coord.x - 1.0)));
    const int inner_top =
        std::max(0, static_cast<int>(std::floor(coord.y - 1.0)));
    const int inner_right =
        std::min(width, static_cast<int>(std::ceil(coord.x - 1.0 + coord.width)));
    const int inner_bottom = std::min(
        height, static_cast<int>(std::ceil(coord.y - 1.0 + coord.height)));

    RgbPixel center_mean;
    std::size_t center_count = 0;
    for (int y = inner_top; y < inner_bottom; ++y) {
      for (int x = inner_left; x < inner_right; ++x) {
        const auto& p = image[static_cast<std::size_t>(y * width + x)];
        center_mean.r += p.r;
        center_mean.g += p.g;
        center_mean.b += p.b;
        ++center_count;
      }
    }
    if (center_count == 0) {
      out.push_back({});
      continue;
    }
    center_mean.r /= static_cast<double>(center_count);
    center_mean.g /= static_cast<double>(center_count);
    center_mean.b /= static_cast<double>(center_count);

    const double center_norm =
        std::sqrt(center_mean.r * center_mean.r +
                  center_mean.g * center_mean.g +
                  center_mean.b * center_mean.b);
    const double sigma = std::max(5.0, 0.12 * center_norm);
    const double two_sigma2 = 2.0 * sigma * sigma;

    const int search_left =
        std::max(0, static_cast<int>(std::floor(cx - search_half_x)));
    const int search_right =
        std::min(width, static_cast<int>(std::ceil(cx + search_half_x)));
    const int search_top =
        std::max(0, static_cast<int>(std::floor(cy - search_half_y)));
    const int search_bottom =
        std::min(height, static_cast<int>(std::ceil(cy + search_half_y)));

    double weight_sum = 0;
    double weighted_x = 0;
    double weighted_y = 0;
    for (int y = search_top; y < search_bottom; ++y) {
      for (int x = search_left; x < search_right; ++x) {
        const auto& p = image[static_cast<std::size_t>(y * width + x)];
        const double dr = p.r - center_mean.r;
        const double dg = p.g - center_mean.g;
        const double db = p.b - center_mean.b;
        const double color_dist2 = dr * dr + dg * dg + db * db;
        const double w = std::exp(-color_dist2 / two_sigma2);
        if (w < 0.05) continue;
        weight_sum += w;
        weighted_x += w * (static_cast<double>(x) + 0.5);
        weighted_y += w * (static_cast<double>(y) + 0.5);
      }
    }

    if (weight_sum < 4.0 || !std::isfinite(weight_sum)) {
      out.push_back({});
      continue;
    }
    out.push_back(
        IndependentPatchCenter{true, weighted_x / weight_sum,
                               weighted_y / weight_sum});
  }
  return out;
}

IndependentCenterRepeatability estimate_independent_center_repeatability(
    const std::vector<IndependentPatchCenter>& a,
    const std::vector<IndependentPatchCenter>& b) {
  if (a.size() != b.size()) {
    throw std::runtime_error(
        "independent center: repeatability inputs have different counts");
  }
  IndependentCenterRepeatability out;
  double sumsq = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!a[i].valid || !b[i].valid) continue;
    sumsq += std::pow(a[i].x - b[i].x, 2) + std::pow(a[i].y - b[i].y, 2);
    ++out.valid_count;
  }
  if (out.valid_count > 0) {
    out.rms_px = std::sqrt(sumsq / static_cast<double>(out.valid_count));
  }
  return out;
}

LocalizationIndependentCenterCheck compare_independent_patch_centers(
    const std::vector<PatchCoord>& generated_coords,
    const RawDiggerPatchTable& oracle,
    const std::vector<IndependentPatchCenter>& independent) {
  if (generated_coords.size() != oracle.coords.size() ||
      generated_coords.size() != independent.size()) {
    throw std::runtime_error(
        "independent center: generated, oracle, and detected counts differ");
  }

  LocalizationIndependentCenterCheck check;
  check.attempted = true;
  check.method = "color_similarity_centroid_from_raw_bilinear_rgb";
  double generated_sumsq = 0;
  double oracle_sumsq = 0;
  for (std::size_t i = 0; i < independent.size(); ++i) {
    if (!independent[i].valid) continue;
    const double gx = patch_center_x(generated_coords[i]);
    const double gy = patch_center_y(generated_coords[i]);
    const double ox = patch_center_x(oracle.coords[i]);
    const double oy = patch_center_y(oracle.coords[i]);
    generated_sumsq +=
        std::pow(independent[i].x - gx, 2) + std::pow(independent[i].y - gy, 2);
    oracle_sumsq +=
        std::pow(independent[i].x - ox, 2) + std::pow(independent[i].y - oy, 2);
    ++check.valid_count;
  }
  if (check.valid_count == 0) {
    check.tracks = "unresolved";
    check.interpretation =
        "independent centroid detector produced no valid patch centres";
    return check;
  }

  check.generated_grid_rms_px =
      std::sqrt(generated_sumsq / static_cast<double>(check.valid_count));
  check.rawdigger_oracle_rms_px =
      std::sqrt(oracle_sumsq / static_cast<double>(check.valid_count));
  constexpr double kTiePx = 0.5;
  constexpr double kMinRelativeSeparation = 0.10;
  const double larger =
      std::max(check.generated_grid_rms_px, check.rawdigger_oracle_rms_px);
  const double separation =
      std::abs(check.generated_grid_rms_px - check.rawdigger_oracle_rms_px);
  const bool separated =
      separation > kTiePx &&
      (larger <= kEpsilon || separation / larger >= kMinRelativeSeparation);
  if (separated &&
      check.generated_grid_rms_px < check.rawdigger_oracle_rms_px) {
    check.tracks = "generated_grid";
    check.interpretation =
        "independent centres are closer to the generated grid than RawDigger";
  } else if (separated &&
             check.rawdigger_oracle_rms_px < check.generated_grid_rms_px) {
    check.tracks = "rawdigger_oracle";
    check.interpretation =
        "independent centres are closer to RawDigger than the generated grid";
  } else {
    check.tracks = "unresolved";
    check.interpretation =
        "independent centres do not separate generated grid from RawDigger";
  }
  return check;
}

void finalize_localization_model_comparison(
    LocalizationModelComparison& comparison,
    const LocalizationIndependentCenterCheck& independent) {
  comparison.noise_floor_px =
      std::max(0.5, independent.repeatability_rms_px);
  comparison.noise_floor_source =
      independent.repeatability_valid_count > 0
          ? "independent_center_repeatability_color_centroid"
          : "fallback_minimum_center_floor";
  constexpr double kMaxUsableNoiseFloorPx = 5.0;
  const std::size_t required_repeatability_count =
      static_cast<std::size_t>(
          std::ceil(0.9 * static_cast<double>(comparison.patch_count)));
  comparison.noise_floor_usable =
      comparison.patch_count > 0 && independent.repeatability_valid_count > 0 &&
      independent.repeatability_valid_count >= required_repeatability_count &&
      comparison.noise_floor_px <= kMaxUsableNoiseFloorPx;

  const LocalizationModelReport* best = nullptr;
  double best_worst = std::numeric_limits<double>::infinity();
  for (const auto& model : comparison.models) {
    if (model.heldout_scores.empty()) continue;
    const double worst = worst_heldout_rms(model);
    if (worst < best_worst) {
      best_worst = worst;
      best = &model;
    }
  }
  if (best) {
    comparison.best_overall_model = best->name;
  }

  if (!comparison.noise_floor_usable) {
    comparison.parsimony_winner_model.clear();
    comparison.conclusive = false;
    comparison.diagnostic_conclusion =
        "unresolved: independent centre repeatability did not provide a "
        "usable noise floor, so held-out residuals cannot be converted into a "
        "parsimony winner";
    return;
  }

  const LocalizationModelReport* parsimonious = nullptr;
  int parsimonious_dof = std::numeric_limits<int>::max();
  for (const auto& model : comparison.models) {
    if (model.heldout_scores.empty()) continue;
    const double worst = worst_heldout_rms(model);
    if (worst <= best_worst + comparison.noise_floor_px &&
        model.degrees_of_freedom < parsimonious_dof) {
      parsimonious = &model;
      parsimonious_dof = model.degrees_of_freedom;
    }
  }
  if (parsimonious) {
    comparison.parsimony_winner_model = parsimonious->name;
  }

  if (independent.tracks == "generated_grid") {
    comparison.parsimony_winner_model.clear();
    comparison.conclusive = false;
    comparison.diagnostic_conclusion =
        "consistent with RawDigger placement mismatch: independent centres "
        "track the generated grid, so held-out residual models describe the "
        "oracle-vs-grid offset rather than a production chart/lens correction";
    return;
  }
  if (independent.tracks != "rawdigger_oracle") {
    comparison.conclusive = false;
    comparison.diagnostic_conclusion =
        "unresolved: independent centre check did not separate generated grid "
        "from RawDigger; off-centre or multi-position capture required before "
        "promoting a causal model";
    return;
  }
  if (comparison.parsimony_winner_model.empty()) {
    comparison.conclusive = false;
    comparison.diagnostic_conclusion =
        "unresolved: no held-out model comparison winner was available";
    return;
  }

  // Strongest reachable grid/model-correction branch: usable noise floor, the
  // independent detector tracks RawDigger over the generated grid, and a
  // parsimony winner exists. Even here conclusive stays false by construction:
  // a near-centred capture cannot separate lens distortion (about the image
  // centre) from chart geometry (about the chart centre), so causal attribution
  // is not identifiable. Do not add a conclusive=true path without a
  // capture-geometry input (off-centre or multi-position) that actually breaks
  // that confound.
  comparison.conclusive = false;
  comparison.diagnostic_conclusion =
      "consistent with " + comparison.parsimony_winner_model +
      " under held-out parsimony, but this centered capture remains "
      "diagnostic-only; production promotion requires a separate validation "
      "slice";
}

}  // namespace camera_iq
