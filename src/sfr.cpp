#include "camera_iq/sfr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include "camera_iq/csv.hpp"

namespace camera_iq {
namespace {

constexpr double kEps = 1e-12;

void parse_edge_id(ImatestYMultiRoi& roi) {
  std::vector<std::string> parts;
  std::string part;
  std::istringstream in(roi.edge_id);
  while (std::getline(in, part, '_')) {
    parts.push_back(part);
  }
  if (parts.size() != 3 && parts.size() != 4) return;
  const auto grid_x = parse_int(parts[0]);
  const auto grid_y = parse_int(parts[1]);
  if (!grid_x || !grid_y || parts[2].empty()) return;
  roi.edge_id_parsed = true;
  roi.edge_id_grid_x = *grid_x;
  roi.edge_id_grid_y = *grid_y;
  roi.edge_id_side = parts[2];
  if (parts.size() == 4) roi.edge_id_suffix = parts[3];
  roi.physical_corner = roi.edge_id_suffix == "C";
  roi.physical_edge = roi.edge_id_suffix == "E";
}

bool is_green(const RawCfaImage& image, int x, int y) {
  const std::size_t pos = static_cast<std::size_t>((y & 1) * 2 + (x & 1));
  const int color_index = image.color_at_position[pos];
  return color_index >= 0 &&
         static_cast<std::size_t>(color_index) < image.cdesc.size() &&
         image.cdesc[static_cast<std::size_t>(color_index)] == 'G';
}

double sample_at(const RawCfaImage& image, int x, int y) {
  return image.samples[static_cast<std::size_t>(y) *
                           static_cast<std::size_t>(image.row_stride_pixels) +
                       static_cast<std::size_t>(x)];
}

SfrResult reject_result(RoiRect roi, std::string reason) {
  SfrResult result;
  result.roi = roi;
  result.rejection_reason = std::move(reason);
  return result;
}

SfrResult reject_result(SfrResult result, std::string reason) {
  result.accepted = false;
  result.rejection_reason = std::move(reason);
  return result;
}

bool valid_sfr_options(const SfrOptions& options) {
  return std::isfinite(options.bin_spacing_px) &&
         options.bin_spacing_px > 0.0 &&
         std::isfinite(options.min_edge_angle_deg) &&
         std::isfinite(options.max_edge_angle_deg) &&
         options.min_edge_angle_deg >= 0.0 &&
         options.max_edge_angle_deg > options.min_edge_angle_deg &&
         options.max_edge_angle_deg <= 90.0 &&
         std::isfinite(options.min_contrast_dn) &&
         options.min_contrast_dn >= 0.0 &&
         std::isfinite(options.near_saturation_fraction) &&
         options.near_saturation_fraction > 0.0 &&
         options.near_saturation_fraction <= 1.0 &&
         options.min_roi_dimension_px >= 2 && options.min_line_samples >= 8;
}

struct LinePoint {
  double scan = 0.0;
  double edge = 0.0;
};

std::optional<double> edge_centroid_for_line(
    const std::vector<std::pair<double, double>>& samples,
    double* line_contrast, std::size_t min_samples) {
  if (samples.size() < min_samples) return std::nullopt;
  const std::size_t q = std::max<std::size_t>(2, samples.size() / 4);
  double start = 0.0;
  double end = 0.0;
  for (std::size_t i = 0; i < q; ++i) {
    start += samples[i].second;
    end += samples[samples.size() - 1 - i].second;
  }
  start /= static_cast<double>(q);
  end /= static_cast<double>(q);
  if (line_contrast) {
    *line_contrast = std::abs(end - start);
  }
  const double threshold = 0.5 * (start + end);
  std::optional<double> coarse;
  double best_step = -1.0;
  for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
    const double v0 = samples[i].second - threshold;
    const double v1 = samples[i + 1].second - threshold;
    const double step = std::abs(samples[i + 1].second - samples[i].second);
    if ((v0 == 0.0 || v0 * v1 <= 0.0) && step > best_step) {
      const double denom = samples[i + 1].second - samples[i].second;
      const double t = std::abs(denom) > kEps
                           ? (threshold - samples[i].second) / denom
                           : 0.5;
      coarse = samples[i].first + std::clamp(t, 0.0, 1.0) *
                                      (samples[i + 1].first - samples[i].first);
      best_step = step;
    }
  }
  if (!coarse) return std::nullopt;

  constexpr double kWindowPx = 8.0;
  double weighted = 0.0;
  double weight_sum = 0.0;
  for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
    const double mid = 0.5 * (samples[i].first + samples[i + 1].first);
    if (std::abs(mid - *coarse) > kWindowPx) continue;
    const double w = std::abs(samples[i + 1].second - samples[i].second);
    weighted += mid * w;
    weight_sum += w;
  }
  if (weight_sum <= kEps) return coarse;
  return weighted / weight_sum;
}

std::optional<std::pair<double, double>> fit_line(
    const std::vector<LinePoint>& points, std::size_t min_samples) {
  if (points.size() < min_samples) return std::nullopt;
  double sx = 0.0;
  double sy = 0.0;
  double sxx = 0.0;
  double sxy = 0.0;
  for (const auto& p : points) {
    sx += p.scan;
    sy += p.edge;
    sxx += p.scan * p.scan;
    sxy += p.scan * p.edge;
  }
  const double n = static_cast<double>(points.size());
  const double denom = n * sxx - sx * sx;
  if (std::abs(denom) <= kEps) return std::nullopt;
  const double slope = (n * sxy - sx * sy) / denom;
  const double intercept = (sy - slope * sx) / n;
  return std::pair{slope, intercept};
}

double interpolate_crossing(const std::vector<double>& x,
                            const std::vector<double>& y, double target) {
  for (std::size_t i = 0; i + 1 < x.size(); ++i) {
    const double y0 = y[i] - target;
    const double y1 = y[i + 1] - target;
    if (y0 == 0.0) return x[i];
    if (y0 * y1 <= 0.0) {
      const double denom = y[i + 1] - y[i];
      const double t = std::abs(denom) > kEps ? (target - y[i]) / denom : 0.0;
      return x[i] + std::clamp(t, 0.0, 1.0) * (x[i + 1] - x[i]);
    }
  }
  return std::numeric_limits<double>::quiet_NaN();
}

double interpolate_curve(const std::vector<double>& x,
                         const std::vector<double>& y, double target_x) {
  if (x.empty() || y.empty() || x.size() != y.size()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (target_x < x.front() || target_x > x.back()) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (target_x == x.front()) return y.front();
  if (target_x == x.back()) return y.back();
  for (std::size_t i = 0; i + 1 < x.size(); ++i) {
    if (target_x >= x[i] && target_x <= x[i + 1]) {
      const double denom = x[i + 1] - x[i];
      const double t = std::abs(denom) > kEps ? (target_x - x[i]) / denom : 0.0;
      return y[i] + t * (y[i + 1] - y[i]);
    }
  }
  return std::numeric_limits<double>::quiet_NaN();
}

double find_mtf_crossing(const std::vector<double>& f,
                         const std::vector<double>& mtf, double target,
                         std::size_t start_index = 0) {
  if (start_index >= f.size() || f.size() != mtf.size()) return 0.0;
  for (std::size_t i = start_index; i + 1 < f.size(); ++i) {
    if (mtf[i] >= target && mtf[i + 1] <= target) {
      const double denom = mtf[i + 1] - mtf[i];
      const double t = std::abs(denom) > kEps ? (target - mtf[i]) / denom : 0.0;
      return f[i] + std::clamp(t, 0.0, 1.0) * (f[i + 1] - f[i]);
    }
  }
  return 0.0;
}

}  // namespace

std::vector<double> dft_magnitude(const std::vector<double>& signal) {
  const std::size_t n = signal.size();
  std::vector<double> out(n / 2 + 1, 0.0);
  for (std::size_t k = 0; k < out.size(); ++k) {
    double re = 0.0;
    double im = 0.0;
    for (std::size_t t = 0; t < n; ++t) {
      const double phase = -2.0 * std::numbers::pi * static_cast<double>(k) *
                           static_cast<double>(t) / static_cast<double>(n);
      re += signal[t] * std::cos(phase);
      im += signal[t] * std::sin(phase);
    }
    out[k] = std::hypot(re, im);
  }
  return out;
}

double adjacent_difference_response(double frequency_cy_per_px,
                                    double sample_spacing_px) {
  const double x = std::numbers::pi * frequency_cy_per_px * sample_spacing_px;
  if (std::abs(x) <= kEps) return 1.0;
  return std::sin(x) / x;
}

SfrResult analyze_green_sfr(const RawCfaImage& image, const RoiRect& requested,
                            const SfrOptions& options) {
  if (!valid_sfr_options(options)) {
    return reject_result(requested, "invalid_options");
  }
  if (image.width <= 0 || image.height <= 0 ||
      image.row_stride_pixels < image.width ||
      image.samples.size() < static_cast<std::size_t>(image.row_stride_pixels) *
                                 static_cast<std::size_t>(image.height) ||
      !std::isfinite(image.meta.white_level) ||
      !std::all_of(
          image.meta.black_per_channel.begin(),
          image.meta.black_per_channel.end(),
          [](double value) { return std::isfinite(value); })) {
    return reject_result(requested, "invalid_raw_image");
  }
  const auto roi = cfa_balanced_roi(requested, image.width, image.height);
  if (!roi || roi->width < options.min_roi_dimension_px ||
      roi->height < options.min_roi_dimension_px) {
    return reject_result(requested, "roi_too_small");
  }

  SfrResult result;
  result.roi = *roi;
  result.oversample = 1.0 / options.bin_spacing_px;

  double dx_sum = 0.0;
  int dx_n = 0;
  double dy_sum = 0.0;
  int dy_n = 0;
  double mn = std::numeric_limits<double>::infinity();
  double mx = -std::numeric_limits<double>::infinity();
  int saturated = 0;
  for (int y = roi->y; y < roi->y + roi->height; ++y) {
    for (int x = roi->x; x < roi->x + roi->width; ++x) {
      if (!is_green(image, x, y)) continue;
      const double v = sample_at(image, x, y);
      if (!std::isfinite(v)) {
        return reject_result(std::move(result), "invalid_raw_image");
      }
      mn = std::min(mn, v);
      mx = std::max(mx, v);
      ++result.green_sample_count;
      if (image.meta.white_level > 0.0) {
        const std::size_t pos = static_cast<std::size_t>((y & 1) * 2 + (x & 1));
        const double raw = v + image.meta.black_per_channel[pos];
        if (raw >= options.near_saturation_fraction * image.meta.white_level) {
          ++saturated;
        }
      }
      if (x + 2 < roi->x + roi->width && is_green(image, x + 2, y)) {
        dx_sum += std::abs(sample_at(image, x + 2, y) - v);
        ++dx_n;
      }
      if (y + 2 < roi->y + roi->height && is_green(image, x, y + 2)) {
        dy_sum += std::abs(sample_at(image, x, y + 2) - v);
        ++dy_n;
      }
    }
  }
  if (result.green_sample_count == 0) {
    return reject_result(std::move(result), "no_green_samples");
  }
  result.saturated_fraction = static_cast<double>(saturated) /
                              static_cast<double>(result.green_sample_count);
  result.contrast_dn = mx - mn;
  if (result.contrast_dn < options.min_contrast_dn) {
    return reject_result(std::move(result), "low_contrast");
  }
  if (result.saturated_fraction > 0.0) {
    return reject_result(std::move(result), "roi_saturated");
  }

  const double dx_mean = dx_n > 0 ? dx_sum / static_cast<double>(dx_n) : 0.0;
  const double dy_mean = dy_n > 0 ? dy_sum / static_cast<double>(dy_n) : 0.0;
  const bool horizontal = dy_mean >= dx_mean;
  result.orientation = horizontal ? "horizontal" : "vertical";

  std::vector<LinePoint> line_points;
  std::vector<double> line_contrasts;
  if (horizontal) {
    for (int x = roi->x; x < roi->x + roi->width; ++x) {
      std::vector<std::pair<double, double>> samples;
      for (int y = roi->y; y < roi->y + roi->height; ++y) {
        if (is_green(image, x, y))
          samples.push_back({static_cast<double>(y), sample_at(image, x, y)});
      }
      double line_contrast = 0.0;
      const auto edge = edge_centroid_for_line(
          samples, &line_contrast,
          static_cast<std::size_t>(options.min_line_samples));
      if (edge) {
        line_points.push_back({static_cast<double>(x), *edge});
        line_contrasts.push_back(line_contrast);
      }
    }
  } else {
    for (int y = roi->y; y < roi->y + roi->height; ++y) {
      std::vector<std::pair<double, double>> samples;
      for (int x = roi->x; x < roi->x + roi->width; ++x) {
        if (is_green(image, x, y))
          samples.push_back({static_cast<double>(x), sample_at(image, x, y)});
      }
      double line_contrast = 0.0;
      const auto edge = edge_centroid_for_line(
          samples, &line_contrast,
          static_cast<std::size_t>(options.min_line_samples));
      if (edge) {
        line_points.push_back({static_cast<double>(y), *edge});
        line_contrasts.push_back(line_contrast);
      }
    }
  }
  const auto line =
      fit_line(line_points, static_cast<std::size_t>(options.min_line_samples));
  if (!line) return reject_result(std::move(result), "edge_fit_failed");
  const double slope = line->first;
  const double intercept = line->second;
  result.edge_angle_deg = std::atan(slope) * 180.0 / std::numbers::pi;
  const double abs_angle = std::abs(result.edge_angle_deg);
  if (abs_angle < options.min_edge_angle_deg ||
      abs_angle > options.max_edge_angle_deg) {
    return reject_result(std::move(result), "edge_angle_out_of_range");
  }

  struct Bin {
    double sum = 0.0;
    int n = 0;
  };
  std::vector<std::pair<double, double>> projected;
  projected.reserve(static_cast<std::size_t>(result.green_sample_count));
  double d_min = std::numeric_limits<double>::infinity();
  double d_max = -std::numeric_limits<double>::infinity();
  for (int y = roi->y; y < roi->y + roi->height; ++y) {
    for (int x = roi->x; x < roi->x + roi->width; ++x) {
      if (!is_green(image, x, y)) continue;
      double d = 0.0;
      if (horizontal) {
        d = (static_cast<double>(y) -
             (slope * static_cast<double>(x) + intercept)) /
            std::sqrt(1.0 + slope * slope);
      } else {
        d = (static_cast<double>(x) -
             (slope * static_cast<double>(y) + intercept)) /
            std::sqrt(1.0 + slope * slope);
      }
      projected.push_back({d, sample_at(image, x, y)});
      d_min = std::min(d_min, d);
      d_max = std::max(d_max, d);
    }
  }
  if (projected.size() < 32 || !(d_max > d_min)) {
    return reject_result(std::move(result), "insufficient_projected_samples");
  }
  const double first_bin_value = std::floor(d_min / options.bin_spacing_px);
  const double last_bin_value = std::ceil(d_max / options.bin_spacing_px);
  constexpr double kMaxEsfBins = 8192.0;
  if (!std::isfinite(first_bin_value) || !std::isfinite(last_bin_value) ||
      first_bin_value < static_cast<double>(std::numeric_limits<int>::min()) ||
      last_bin_value > static_cast<double>(std::numeric_limits<int>::max()) ||
      last_bin_value < first_bin_value ||
      last_bin_value - first_bin_value + 1.0 > kMaxEsfBins) {
    return reject_result(std::move(result), "invalid_esf_grid");
  }
  const int first_bin = static_cast<int>(first_bin_value);
  const int last_bin = static_cast<int>(last_bin_value);
  std::vector<Bin> bins(static_cast<std::size_t>(last_bin - first_bin + 1));
  for (const auto& p : projected) {
    const int idx =
        static_cast<int>(std::floor(p.first / options.bin_spacing_px)) -
        first_bin;
    if (idx >= 0 && static_cast<std::size_t>(idx) < bins.size()) {
      bins[static_cast<std::size_t>(idx)].sum += p.second;
      ++bins[static_cast<std::size_t>(idx)].n;
    }
  }

  const auto first_filled = std::find_if(
      bins.begin(), bins.end(), [](const Bin& bin) { return bin.n > 0; });
  const auto last_filled = std::find_if(
      bins.rbegin(), bins.rend(), [](const Bin& bin) { return bin.n > 0; });
  if (first_filled == bins.end() || last_filled == bins.rend()) {
    return reject_result(std::move(result), "underfilled_esf");
  }
  const std::size_t first_filled_index =
      static_cast<std::size_t>(std::distance(bins.begin(), first_filled));
  const std::size_t last_filled_index =
      bins.size() - 1 -
      static_cast<std::size_t>(std::distance(bins.rbegin(), last_filled));

  std::vector<double> x;
  std::vector<double> esf;
  x.reserve(last_filled_index - first_filled_index + 1);
  esf.reserve(last_filled_index - first_filled_index + 1);
  for (std::size_t i = first_filled_index; i <= last_filled_index; ++i) {
    x.push_back(
        (static_cast<double>(first_bin) + static_cast<double>(i) + 0.5) *
        options.bin_spacing_px);
    esf.push_back(bins[i].n > 0 ? bins[i].sum / static_cast<double>(bins[i].n)
                                : std::numeric_limits<double>::quiet_NaN());
  }
  std::size_t missing_bins = 0;
  std::size_t max_gap = 0;
  std::size_t current_gap = 0;
  for (const double value : esf) {
    if (std::isfinite(value)) {
      current_gap = 0;
      continue;
    }
    ++missing_bins;
    ++current_gap;
    max_gap = std::max(max_gap, current_gap);
  }
  // Keep interpolation bounded: a minority of missing fine-grid bins is
  // recoverable, but long holes or a mostly synthesized ESF are not evidence.
  constexpr std::size_t kMaxInterpolatedGapBins = 16;
  constexpr double kMaxInterpolatedFraction = 0.45;
  if (max_gap > kMaxInterpolatedGapBins ||
      static_cast<double>(missing_bins) / static_cast<double>(esf.size()) >
          kMaxInterpolatedFraction) {
    return reject_result(std::move(result), "underfilled_esf");
  }
  for (std::size_t i = 0; i < esf.size();) {
    if (std::isfinite(esf[i])) {
      ++i;
      continue;
    }
    const std::size_t gap_begin = i;
    while (i < esf.size() && !std::isfinite(esf[i])) ++i;
    const std::size_t gap_end = i;
    const double left = esf[gap_begin - 1];
    const double right = esf[gap_end];
    const double span = static_cast<double>(gap_end - gap_begin + 1);
    for (std::size_t j = gap_begin; j < gap_end; ++j) {
      const double t = static_cast<double>(j - gap_begin + 1) / span;
      esf[j] = left + t * (right - left);
    }
  }
  if (esf.size() < 32) {
    return reject_result(std::move(result), "underfilled_esf");
  }

  const std::size_t tail = std::max<std::size_t>(4, esf.size() / 10);
  const double start_mean =
      std::accumulate(esf.begin(), esf.begin() + static_cast<long>(tail), 0.0) /
      static_cast<double>(tail);
  const double end_mean =
      std::accumulate(esf.end() - static_cast<long>(tail), esf.end(), 0.0) /
      static_cast<double>(tail);
  if (start_mean > end_mean) {
    std::reverse(esf.begin(), esf.end());
    for (double& value : x) value = -value;
    std::reverse(x.begin(), x.end());
  }
  const auto [esf_min_it, esf_max_it] =
      std::minmax_element(esf.begin(), esf.end());
  const double esf_min = *esf_min_it;
  const double esf_range = *esf_max_it - esf_min;
  if (esf_range <= kEps) {
    return reject_result(std::move(result), "low_contrast");
  }
  for (double& value : esf) value = (value - esf_min) / esf_range;

  const double x10 = interpolate_crossing(x, esf, 0.1);
  const double x90 = interpolate_crossing(x, esf, 0.9);
  if (std::isfinite(x10) && std::isfinite(x90)) {
    result.r1090_px = std::abs(x90 - x10);
  }

  std::vector<double> lsf;
  lsf.reserve(esf.size() - 1);
  for (std::size_t i = 0; i + 1 < esf.size(); ++i) {
    lsf.push_back(esf[i + 1] - esf[i]);
  }
  for (std::size_t i = 0; i < lsf.size(); ++i) {
    const double w =
        0.54 - 0.46 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                               static_cast<double>(lsf.size() - 1));
    lsf[i] *= w;
  }
  const auto mag = dft_magnitude(lsf);
  if (mag.empty() || mag[0] <= kEps) {
    return reject_result(std::move(result), "dc_normalization_zero");
  }

  result.mtf_frequency_cy_per_px.reserve(mag.size());
  result.mtf.reserve(mag.size());
  for (std::size_t k = 0; k < mag.size(); ++k) {
    const double f =
        (static_cast<double>(k) / static_cast<double>(lsf.size())) /
        options.bin_spacing_px;
    double mtf = mag[k] / mag[0];
    const double response =
        adjacent_difference_response(f, options.bin_spacing_px);
    if (response > kEps) mtf /= response;
    result.mtf_frequency_cy_per_px.push_back(f);
    result.mtf.push_back(mtf);
  }
  if (result.mtf_frequency_cy_per_px.empty() ||
      result.mtf_frequency_cy_per_px.front() > 0.5 ||
      result.mtf_frequency_cy_per_px.back() < 0.5) {
    return reject_result(std::move(result), "nyquist_not_sampled");
  }
  result.mtf_at_nyquist =
      interpolate_curve(result.mtf_frequency_cy_per_px, result.mtf, 0.5);
  if (!std::isfinite(result.mtf_at_nyquist)) {
    return reject_result(std::move(result), "nyquist_not_sampled");
  }
  result.mtf50_cy_per_px =
      find_mtf_crossing(result.mtf_frequency_cy_per_px, result.mtf, 0.5);
  const auto peak_it = std::max_element(result.mtf.begin(), result.mtf.end());
  const double peak = *peak_it;
  const std::size_t peak_index =
      static_cast<std::size_t>(std::distance(result.mtf.begin(), peak_it));
  std::vector<double> mtfp = result.mtf;
  if (peak > kEps) {
    for (double& v : mtfp) v /= peak;
    result.mtf50p_cy_per_px = find_mtf_crossing(result.mtf_frequency_cy_per_px,
                                                mtfp, 0.5, peak_index);
  }
  if (result.mtf50_cy_per_px <= 0.0) {
    return reject_result(std::move(result), "mtf50_not_found");
  }
  if (!std::isfinite(result.mtf50p_cy_per_px) ||
      result.mtf50p_cy_per_px <= 0.0) {
    return reject_result(std::move(result), "mtf50p_not_found");
  }
  result.accepted = true;
  return result;
}

std::optional<ImatestYMultiOracle> read_imatest_y_multi(
    const std::filesystem::path& path) {
  ImatestYMultiOracle oracle;
  std::ifstream is(path);
  if (!is) return std::nullopt;
  std::string line;
  bool in_roi_table = false;
  bool in_mtf_table = false;
  bool have_center_roi = false;
  bool have_center_mtf = false;
  while (std::getline(is, line)) {
    const auto cells = split_csv_line(line);
    if (cells.empty()) {
      in_roi_table = false;
      in_mtf_table = false;
      continue;
    }
    if (cells[0] == "File" && cells.size() > 1) {
      oracle.filename = cells[1];
      continue;
    }
    if (cells[0] == "Run date" && cells.size() > 1) {
      oracle.run_date = cells[1];
      continue;
    }
    if (cells.size() > 8 && cells[0] == "N" && cells[1] == "Distance %") {
      in_roi_table = true;
      in_mtf_table = false;
      continue;
    }
    if (cells.size() > 7 && cells[0] == "N" &&
        cells[1].find("MTF50") != std::string::npos) {
      in_roi_table = false;
      in_mtf_table = true;
      continue;
    }
    if (in_roi_table && cells.size() > 8 && cells[0] == "1") {
      if (have_center_roi) return std::nullopt;
      const auto x1 = parse_int(cells[3]);
      const auto y1 = parse_int(cells[4]);
      const auto x2 = parse_int(cells[5]);
      const auto y2 = parse_int(cells[6]);
      const auto width = parse_int(cells[7]);
      const auto height = parse_int(cells[8]);
      if (!x1 || !y1 || !x2 || !y2 || !width || !height || *width <= 0 ||
          *height <= 0 ||
          static_cast<std::int64_t>(*x2) - static_cast<std::int64_t>(*x1) + 1 !=
              static_cast<std::int64_t>(*width) ||
          static_cast<std::int64_t>(*y2) - static_cast<std::int64_t>(*y1) + 1 !=
              static_cast<std::int64_t>(*height)) {
        return std::nullopt;
      }
      oracle.center_roi_full_frame = RoiRect{*x1, *y1, *width, *height};
      have_center_roi = true;
      continue;
    }
    if (in_mtf_table && cells.size() > 7 && cells[0] == "1") {
      if (have_center_mtf) return std::nullopt;
      const auto mtf50 = parse_double(cells[1]);
      const auto mtf50p = parse_double(cells[7]);
      if (!mtf50 || !mtf50p) return std::nullopt;
      oracle.center_mtf50_cy_per_px = *mtf50;
      oracle.center_mtf50p_cy_per_px = *mtf50p;
      have_center_mtf = true;
      continue;
    }
  }
  if (oracle.filename.empty() || oracle.run_date.empty() || !have_center_roi ||
      !have_center_mtf || oracle.center_roi_full_frame.width <= 0 ||
      oracle.center_roi_full_frame.height <= 0 ||
      oracle.center_mtf50_cy_per_px <= 0.0 ||
      oracle.center_mtf50p_cy_per_px <= 0.0) {
    return std::nullopt;
  }
  return oracle;
}

std::optional<ImatestYMultiFile> read_imatest_y_multi_file(
    const std::filesystem::path& path) {
  std::ifstream is(path);
  if (!is) return std::nullopt;
  ImatestYMultiFile file;
  std::string line;
  bool in_roi_table = false;
  bool in_mtf_table = false;
  std::map<int, ImatestYMultiRoi> roi_by_n;
  struct MtfRow {
    double mtf50 = 0.0;
    double mtf50p = 0.0;
    double r1090 = 0.0;
    double peak = 0.0;
  };
  std::map<int, MtfRow> mtf_by_n;
  while (std::getline(is, line)) {
    const auto cells = split_csv_line(line);
    if (cells.empty()) {
      in_roi_table = false;
      in_mtf_table = false;
      continue;
    }
    if (cells[0] == "File" && cells.size() > 1) {
      file.filename = cells[1];
      continue;
    }
    if (cells[0] == "Run date" && cells.size() > 1) {
      file.run_date = cells[1];
      continue;
    }
    if (cells.size() > 8 && cells[0] == "N" && cells[1] == "Distance %") {
      in_roi_table = true;
      in_mtf_table = false;
      continue;
    }
    if (cells.size() > 7 && cells[0] == "N" &&
        cells[1].find("MTF50") != std::string::npos) {
      in_roi_table = false;
      in_mtf_table = true;
      continue;
    }
    if (in_roi_table && cells.size() > 10) {
      const auto n = parse_int(cells[0]);
      if (!n) {
        in_roi_table = false;
        continue;
      }
      if (roi_by_n.count(*n) > 0) return std::nullopt;
      const auto distance = parse_double(cells[1]);
      const auto x1 = parse_int(cells[3]);
      const auto y1 = parse_int(cells[4]);
      const auto x2 = parse_int(cells[5]);
      const auto y2 = parse_int(cells[6]);
      const auto width = parse_int(cells[7]);
      const auto height = parse_int(cells[8]);
      if (!distance || !x1 || !y1 || !x2 || !y2 || !width || !height ||
          *width <= 0 || *height <= 0 ||
          static_cast<std::int64_t>(*x2) - static_cast<std::int64_t>(*x1) + 1 !=
              static_cast<std::int64_t>(*width) ||
          static_cast<std::int64_t>(*y2) - static_cast<std::int64_t>(*y1) + 1 !=
              static_cast<std::int64_t>(*height)) {
        return std::nullopt;
      }
      ImatestYMultiRoi roi;
      roi.n = *n;
      roi.distance_percent = *distance;
      roi.direction_label = cells[2];
      roi.region_label = cells[9];
      roi.edge_id = cells[10];
      if (cells.size() > 13 && !cells[13].empty()) {
        roi.csv_summary_file =
            std::filesystem::path(cells[13]).filename().string();
      }
      roi.full_frame_roi = RoiRect{*x1, *y1, *width, *height};
      if (cells.size() > 11) {
        const auto x_ctr = parse_double(cells[11]);
        if (x_ctr) roi.x_px_from_ctr = *x_ctr;
      }
      if (cells.size() > 12) {
        const auto y_ctr = parse_double(cells[12]);
        if (y_ctr) roi.y_px_from_ctr = *y_ctr;
      }
      parse_edge_id(roi);
      if (!roi.edge_id_parsed) return std::nullopt;
      roi_by_n.emplace(*n, std::move(roi));
      continue;
    }
    if (in_mtf_table && cells.size() > 7) {
      const auto n = parse_int(cells[0]);
      if (!n) {
        in_mtf_table = false;
        continue;
      }
      if (mtf_by_n.count(*n) > 0) return std::nullopt;
      const auto mtf50 = parse_double(cells[1]);
      const auto r1090 = parse_double(cells[2]);
      const auto peak = parse_double(cells[6]);
      const auto mtf50p = parse_double(cells[7]);
      if (!mtf50 || !r1090 || !peak || !mtf50p) return std::nullopt;
      mtf_by_n.emplace(*n, MtfRow{*mtf50, *mtf50p, *r1090, *peak});
      continue;
    }
  }
  if (file.filename.empty() || file.run_date.empty() || roi_by_n.size() != 23 ||
      roi_by_n.size() != mtf_by_n.size()) {
    return std::nullopt;
  }
  int expected_n = 1;
  for (const auto& [n, _] : roi_by_n) {
    if (n != expected_n++) return std::nullopt;
  }
  expected_n = 1;
  for (const auto& [n, _] : mtf_by_n) {
    if (n != expected_n++) return std::nullopt;
  }

  std::set<std::string> edge_ids;
  std::set<std::pair<int, int>> grid_positions;
  std::set<std::pair<int, int>> physical_corners;
  for (const auto& [n, roi] : roi_by_n) {
    const auto grid_position =
        std::pair{roi.edge_id_grid_x, roi.edge_id_grid_y};
    if (!edge_ids.insert(roi.edge_id).second ||
        !grid_positions.insert(grid_position).second) {
      return std::nullopt;
    }
    if (n == 1 && (grid_position != std::pair{0, 0} || roi.physical_corner ||
                   roi.physical_edge)) {
      return std::nullopt;
    }
    if (roi.physical_corner) physical_corners.insert(grid_position);
  }
  const std::set<std::pair<int, int>> expected_physical_corners = {
      {-4, -2}, {4, -2}, {-4, 2}, {4, 2}};
  if (physical_corners != expected_physical_corners) return std::nullopt;

  file.rois.reserve(roi_by_n.size());
  for (auto& [n, roi] : roi_by_n) {
    const auto mtf = mtf_by_n.find(n);
    if (mtf == mtf_by_n.end()) return std::nullopt;
    roi.imatest_mtf50_cy_per_px = mtf->second.mtf50;
    roi.imatest_mtf50p_cy_per_px = mtf->second.mtf50p;
    roi.imatest_r1090_px = mtf->second.r1090;
    roi.imatest_peak_mtf = mtf->second.peak;
    if (roi.imatest_mtf50_cy_per_px <= 0.0 ||
        roi.imatest_mtf50p_cy_per_px <= 0.0) {
      return std::nullopt;
    }
    file.rois.push_back(std::move(roi));
  }
  return file;
}

std::optional<SfrFieldMtfSummary> summarize_imatest_field_mtf(
    const ImatestYMultiFile& file) {
  if (file.rois.empty()) return std::nullopt;
  SfrFieldMtfSummary summary;
  summary.roi_count = static_cast<int>(file.rois.size());

  bool have_center = false;
  bool have_corner = false;
  bool have_argmax = false;
  for (const auto& roi : file.rois) {
    const double mtf50 = roi.imatest_mtf50_cy_per_px;
    if (!(mtf50 > 0.0) || !std::isfinite(mtf50)) return std::nullopt;
    if (!have_argmax || mtf50 > summary.field_argmax_mtf50_cy_per_px) {
      summary.field_argmax_mtf50_cy_per_px = mtf50;
      summary.field_argmax_n = roi.n;
      have_argmax = true;
    }
    if (roi.n == 1) {
      summary.center_mtf50_cy_per_px = mtf50;
      have_center = true;
    }
    if (roi.physical_corner) {
      ++summary.physical_corner_count;
      summary.physical_corner_max_mtf50_cy_per_px =
          have_corner
              ? std::max(summary.physical_corner_max_mtf50_cy_per_px, mtf50)
              : mtf50;
      have_corner = true;
    }
  }

  if (!have_center || !have_corner || !have_argmax) return std::nullopt;
  summary.center_above_physical_corner_max =
      summary.center_mtf50_cy_per_px >
      summary.physical_corner_max_mtf50_cy_per_px;
  summary.center_is_field_max = summary.field_argmax_n == 1;
  return summary;
}

std::optional<RoiRect> full_frame_roi_to_active_area(const RoiRect& full_frame,
                                                     const RawMeta& meta) {
  const std::int64_t active_x = static_cast<std::int64_t>(full_frame.x) -
                                static_cast<std::int64_t>(meta.left_margin);
  const std::int64_t active_y = static_cast<std::int64_t>(full_frame.y) -
                                static_cast<std::int64_t>(meta.top_margin);
  if (active_x < std::numeric_limits<int>::min() ||
      active_x > std::numeric_limits<int>::max() ||
      active_y < std::numeric_limits<int>::min() ||
      active_y > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  RoiRect active{static_cast<int>(active_x), static_cast<int>(active_y),
                 full_frame.width, full_frame.height};
  return cfa_balanced_roi(active, meta.visible_width, meta.visible_height);
}

SfrTrendResult evaluate_aperture_trend(
    const std::vector<SfrSweepPoint>& points) {
  SfrTrendResult out;
  std::optional<double> f16;
  std::vector<double> wide;
  std::vector<double> mid;
  for (const auto& p : points) {
    if (std::abs(p.aperture - 16.0) < 1e-9) {
      f16 = p.mtf50_cy_per_px;
    }
    if (p.aperture == 1.4 || p.aperture == 1.8 || p.aperture == 2.0) {
      wide.push_back(p.mtf50_cy_per_px);
    }
    if (p.aperture == 4.0 || p.aperture == 5.6 || p.aperture == 8.0 ||
        p.aperture == 11.0) {
      mid.push_back(p.mtf50_cy_per_px);
    }
    if (p.mtf50_cy_per_px > out.argmax_mtf50) {
      out.argmax_mtf50 = p.mtf50_cy_per_px;
      out.argmax_aperture = p.aperture;
    }
  }
  if (!f16 || wide.empty() || mid.empty()) return out;
  out.f16_value = *f16;
  out.wide_open_max = *std::max_element(wide.begin(), wide.end());
  out.mid_plateau_min = *std::min_element(mid.begin(), mid.end());
  out.passed = out.mid_plateau_min > out.f16_value &&
               out.f16_value > out.wide_open_max &&
               (out.argmax_aperture == 4.0 || out.argmax_aperture == 5.6 ||
                out.argmax_aperture == 8.0 || out.argmax_aperture == 11.0);
  return out;
}

}  // namespace camera_iq
