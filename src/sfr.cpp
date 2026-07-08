#include "camera_iq/sfr.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>

namespace camera_iq {
namespace {

constexpr double kEps = 1e-12;

std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> out;
  std::string cell;
  std::istringstream in(line);
  while (std::getline(in, cell, ',')) {
    const auto first = cell.find_first_not_of(" \t\r\n");
    const auto last = cell.find_last_not_of(" \t\r\n");
    out.push_back(first == std::string::npos
                      ? std::string{}
                      : cell.substr(first, last - first + 1));
  }
  if (!line.empty() && line.back() == ',') out.emplace_back();
  return out;
}

std::optional<double> parse_double(const std::string& text) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<int> parse_int(const std::string& text) {
  const auto value = parse_double(text);
  if (!value) return std::nullopt;
  const double rounded = std::round(*value);
  if (std::abs(*value - rounded) > 1e-9) return std::nullopt;
  return static_cast<int>(rounded);
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

struct LinePoint {
  double scan = 0.0;
  double edge = 0.0;
};

std::optional<double> edge_centroid_for_line(
    const std::vector<std::pair<double, double>>& samples,
    double* line_contrast) {
  if (samples.size() < 8) return std::nullopt;
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
      coarse = samples[i].first +
               std::clamp(t, 0.0, 1.0) *
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
    const std::vector<LinePoint>& points) {
  if (points.size() < 8) return std::nullopt;
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
  if (x.empty() || y.empty() || x.size() != y.size()) return 0.0;
  if (target_x <= x.front()) return y.front();
  if (target_x >= x.back()) return y.back();
  for (std::size_t i = 0; i + 1 < x.size(); ++i) {
    if (target_x >= x[i] && target_x <= x[i + 1]) {
      const double denom = x[i + 1] - x[i];
      const double t = std::abs(denom) > kEps ? (target_x - x[i]) / denom : 0.0;
      return y[i] + t * (y[i + 1] - y[i]);
    }
  }
  return y.back();
}

double find_mtf_crossing(const std::vector<double>& f,
                         const std::vector<double>& mtf, double target) {
  for (std::size_t i = 1; i + 1 < f.size(); ++i) {
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
  if (image.width <= 0 || image.height <= 0 ||
      image.row_stride_pixels < image.width ||
      image.samples.size() < static_cast<std::size_t>(image.row_stride_pixels) *
                                 static_cast<std::size_t>(image.height)) {
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
    return reject_result(*roi, "no_green_samples");
  }
  result.saturated_fraction =
      static_cast<double>(saturated) / static_cast<double>(result.green_sample_count);
  result.contrast_dn = mx - mn;
  if (result.contrast_dn < options.min_contrast_dn) {
    return reject_result(*roi, "low_contrast");
  }
  if (result.saturated_fraction > 0.0) {
    return reject_result(*roi, "roi_saturated");
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
        if (is_green(image, x, y)) samples.push_back({static_cast<double>(y),
                                                      sample_at(image, x, y)});
      }
      double line_contrast = 0.0;
      const auto edge = edge_centroid_for_line(samples, &line_contrast);
      if (edge) {
        line_points.push_back({static_cast<double>(x), *edge});
        line_contrasts.push_back(line_contrast);
      }
    }
  } else {
    for (int y = roi->y; y < roi->y + roi->height; ++y) {
      std::vector<std::pair<double, double>> samples;
      for (int x = roi->x; x < roi->x + roi->width; ++x) {
        if (is_green(image, x, y)) samples.push_back({static_cast<double>(x),
                                                      sample_at(image, x, y)});
      }
      double line_contrast = 0.0;
      const auto edge = edge_centroid_for_line(samples, &line_contrast);
      if (edge) {
        line_points.push_back({static_cast<double>(y), *edge});
        line_contrasts.push_back(line_contrast);
      }
    }
  }
  const auto line = fit_line(line_points);
  if (!line) return reject_result(*roi, "edge_fit_failed");
  const double slope = line->first;
  const double intercept = line->second;
  result.edge_angle_deg = std::atan(slope) * 180.0 / std::numbers::pi;
  const double abs_angle = std::abs(result.edge_angle_deg);
  if (abs_angle < options.min_edge_angle_deg ||
      abs_angle > options.max_edge_angle_deg) {
    return reject_result(*roi, "edge_angle_out_of_range");
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
    return reject_result(*roi, "insufficient_projected_samples");
  }
  const int first_bin =
      static_cast<int>(std::floor(d_min / options.bin_spacing_px));
  const int last_bin =
      static_cast<int>(std::ceil(d_max / options.bin_spacing_px));
  std::vector<Bin> bins(static_cast<std::size_t>(last_bin - first_bin + 1));
  for (const auto& p : projected) {
    const int idx = static_cast<int>(std::floor(p.first / options.bin_spacing_px)) -
                    first_bin;
    if (idx >= 0 && static_cast<std::size_t>(idx) < bins.size()) {
      bins[static_cast<std::size_t>(idx)].sum += p.second;
      ++bins[static_cast<std::size_t>(idx)].n;
    }
  }

  std::vector<double> x;
  std::vector<double> esf;
  x.reserve(bins.size());
  esf.reserve(bins.size());
  for (std::size_t i = 0; i < bins.size(); ++i) {
    if (bins[i].n <= 0) continue;
    x.push_back((static_cast<double>(first_bin) + static_cast<double>(i) + 0.5) *
                options.bin_spacing_px);
    esf.push_back(bins[i].sum / static_cast<double>(bins[i].n));
  }
  if (esf.size() < 32) return reject_result(*roi, "underfilled_esf");

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
  if (esf_range <= kEps) return reject_result(*roi, "low_contrast");
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
  if (lsf.size() < 16) return reject_result(*roi, "underfilled_lsf");
  for (std::size_t i = 0; i < lsf.size(); ++i) {
    const double w =
        0.54 - 0.46 * std::cos(2.0 * std::numbers::pi *
                               static_cast<double>(i) /
                               static_cast<double>(lsf.size() - 1));
    lsf[i] *= w;
  }
  const auto mag = dft_magnitude(lsf);
  if (mag.empty() || mag[0] <= kEps) {
    return reject_result(*roi, "dc_normalization_zero");
  }

  result.mtf_frequency_cy_per_px.reserve(mag.size());
  result.mtf.reserve(mag.size());
  for (std::size_t k = 0; k < mag.size(); ++k) {
    const double f = (static_cast<double>(k) / static_cast<double>(lsf.size())) /
                     options.bin_spacing_px;
    double mtf = mag[k] / mag[0];
    const double response = adjacent_difference_response(f, options.bin_spacing_px);
    if (response > kEps) mtf /= response;
    result.mtf_frequency_cy_per_px.push_back(f);
    result.mtf.push_back(mtf);
  }
  result.mtf_at_nyquist =
      interpolate_curve(result.mtf_frequency_cy_per_px, result.mtf, 0.5);
  result.mtf50_cy_per_px =
      find_mtf_crossing(result.mtf_frequency_cy_per_px, result.mtf, 0.5);
  const double peak = *std::max_element(result.mtf.begin(), result.mtf.end());
  std::vector<double> mtfp = result.mtf;
  if (peak > kEps) {
    for (double& v : mtfp) v /= peak;
    result.mtf50p_cy_per_px =
        find_mtf_crossing(result.mtf_frequency_cy_per_px, mtfp, 0.5);
  }
  if (result.mtf50_cy_per_px <= 0.0) {
    return reject_result(*roi, "mtf50_not_found");
  }

  result.accepted = true;
  return result;
}

std::optional<ImatestYMultiOracle> read_imatest_y_multi(
    const std::filesystem::path& path) {
  std::ifstream is(path);
  if (!is) return std::nullopt;
  ImatestYMultiOracle oracle;
  std::string line;
  bool in_roi_table = false;
  bool in_mtf_table = false;
  while (std::getline(is, line)) {
    const auto cells = split_csv_line(line);
    if (cells.empty()) continue;
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
      const auto x1 = parse_int(cells[3]);
      const auto y1 = parse_int(cells[4]);
      const auto width = parse_int(cells[7]);
      const auto height = parse_int(cells[8]);
      if (!x1 || !y1 || !width || !height) return std::nullopt;
      oracle.center_roi_full_frame = RoiRect{*x1, *y1, *width, *height};
      continue;
    }
    if (in_mtf_table && cells.size() > 7 && cells[0] == "1") {
      const auto mtf50 = parse_double(cells[1]);
      const auto mtf50p = parse_double(cells[7]);
      if (!mtf50 || !mtf50p) return std::nullopt;
      oracle.center_mtf50_cy_per_px = *mtf50;
      oracle.center_mtf50p_cy_per_px = *mtf50p;
      break;
    }
  }
  if (oracle.filename.empty() || oracle.run_date.empty() ||
      oracle.center_roi_full_frame.width <= 0 ||
      oracle.center_mtf50_cy_per_px <= 0.0) {
    return std::nullopt;
  }
  return oracle;
}

std::optional<RoiRect> full_frame_roi_to_active_area(const RoiRect& full_frame,
                                                     const RawMeta& meta) {
  RoiRect active{full_frame.x - meta.left_margin,
                 full_frame.y - meta.top_margin,
                 full_frame.width,
                 full_frame.height};
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
