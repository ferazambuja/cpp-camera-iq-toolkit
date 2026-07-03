#include "camera_iq/roi.hpp"

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/raw_meta.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace camera_iq {

std::optional<RoiRect> parse_roi_spec(std::string_view spec) {
  std::istringstream in{std::string(spec)};
  RoiRect roi;
  char c1 = 0, c2 = 0, c3 = 0;
  if (!(in >> roi.x >> c1 >> roi.y >> c2 >> roi.width >> c3 >>
        roi.height)) {
    return std::nullopt;
  }
  if (c1 != ',' || c2 != ',' || c3 != ',') return std::nullopt;
  in >> std::ws;
  if (!in.eof()) return std::nullopt;
  if (roi.x < 0 || roi.y < 0 || roi.width <= 0 || roi.height <= 0) {
    return std::nullopt;
  }
  return roi;
}

std::optional<RoiRect> cfa_balanced_roi(const RoiRect& requested,
                                        int image_width, int image_height) {
  if (image_width <= 0 || image_height <= 0 || requested.width <= 0 ||
      requested.height <= 0) {
    return std::nullopt;
  }

  const long long req_x1 =
      static_cast<long long>(requested.x) + requested.width;
  const long long req_y1 =
      static_cast<long long>(requested.y) + requested.height;

  int x0 = std::clamp(requested.x, 0, image_width);
  int y0 = std::clamp(requested.y, 0, image_height);
  int x1 = static_cast<int>(
      std::clamp(req_x1, 0LL, static_cast<long long>(image_width)));
  int y1 = static_cast<int>(
      std::clamp(req_y1, 0LL, static_cast<long long>(image_height)));

  if (x0 & 1) ++x0;
  if (y0 & 1) ++y0;
  if (x1 & 1) --x1;
  if (y1 & 1) --y1;

  if (x1 - x0 < 2 || y1 - y0 < 2) return std::nullopt;
  return RoiRect{x0, y0, x1 - x0, y1 - y0};
}

namespace {

struct Acc {
  double mean = 0.0;
  double m2 = 0.0;
  double mn = 0.0;
  double mx = 0.0;
  std::size_t n = 0;
  std::size_t below_black = 0;
  std::size_t sat = 0;
  bool init = false;
};

void add_sample(Acc& a, double residual, double raw, double white) {
  ++a.n;
  const double delta = residual - a.mean;
  a.mean += delta / static_cast<double>(a.n);
  a.m2 += delta * (residual - a.mean);
  if (residual < 0.0) ++a.below_black;
  if (raw >= white) ++a.sat;
  if (!a.init) {
    a.mn = a.mx = residual;
    a.init = true;
  } else {
    if (residual < a.mn) a.mn = residual;
    if (residual > a.mx) a.mx = residual;
  }
}

}  // namespace

std::optional<RawCfaReport> raw_cfa_report_for_roi(const RawCfaImage& image,
                                                   const RoiRect& requested) {
  const auto roi = cfa_balanced_roi(requested, image.width, image.height);
  if (!roi || image.row_stride_pixels < image.width ||
      image.samples.size() <
          static_cast<std::size_t>(image.row_stride_pixels) *
              static_cast<std::size_t>(image.height)) {
    return std::nullopt;
  }

  const auto labels = channel_labels(image.cdesc, image.color_at_position);
  std::array<Acc, 4> acc;

  for (int r = roi->y; r < roi->y + roi->height; ++r) {
    const std::size_t row = static_cast<std::size_t>(r) *
                            static_cast<std::size_t>(image.row_stride_pixels);
    for (int c = roi->x; c < roi->x + roi->width; ++c) {
      const std::size_t pos = static_cast<std::size_t>((r & 1) * 2 + (c & 1));
      const double residual = image.samples[row + static_cast<std::size_t>(c)];
      const double raw = residual + image.meta.black_per_channel[pos];
      add_sample(acc[pos], residual, raw, image.meta.white_level);
    }
  }

  RawCfaReport report;
  report.meta = image.meta;
  report.measurement_roi = *roi;
  for (int p = 0; p < 4; ++p) {
    const Acc& a = acc[static_cast<std::size_t>(p)];
    ChannelStats& s = report.planes[static_cast<std::size_t>(p)];
    s.label = labels[static_cast<std::size_t>(p)];
    s.count = a.n;
    if (a.n > 0) {
      const double n = static_cast<double>(a.n);
      s.mean = a.mean;
      const double var = a.m2 / n;
      s.stddev = var > 0.0 ? std::sqrt(var) : 0.0;
      s.min = a.mn;
      s.max = a.mx;
      s.below_black_fraction = static_cast<double>(a.below_black) / n;
      s.saturated_fraction = static_cast<double>(a.sat) / n;
    }
  }
  return report;
}

}  // namespace camera_iq
