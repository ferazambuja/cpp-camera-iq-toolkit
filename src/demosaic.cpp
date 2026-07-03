#include "camera_iq/demosaic.hpp"

#include <cmath>
#include <cstddef>

namespace camera_iq {
namespace {

int rgb_component_for_index(int color_index, std::string_view cdesc) {
  if (color_index < 0 || static_cast<std::size_t>(color_index) >= cdesc.size()) {
    return -1;
  }
  switch (cdesc[static_cast<std::size_t>(color_index)]) {
    case 'R': return 0;
    case 'G': return 1;
    case 'B': return 2;
    default: return -1;
  }
}

double component_value(const RgbPixel& p, int component) {
  switch (component) {
    case 0: return p.r;
    case 1: return p.g;
    default: return p.b;
  }
}

}  // namespace

std::vector<RgbPixel> demosaic_bilinear(
    const double* data, int width, int height, int row_stride_pixels,
    const std::array<int, 4>& color_at_position, std::string_view cdesc) {
  if (data == nullptr || width <= 0 || height <= 0 || row_stride_pixels < width) {
    return {};
  }

  std::array<int, 4> component_at_position{};
  for (int p = 0; p < 4; ++p) {
    component_at_position[static_cast<std::size_t>(p)] =
        rgb_component_for_index(color_at_position[static_cast<std::size_t>(p)],
                                cdesc);
    if (component_at_position[static_cast<std::size_t>(p)] < 0) return {};
  }

  const auto component_at = [&](int r, int c) {
    return component_at_position[static_cast<std::size_t>((r & 1) * 2 + (c & 1))];
  };
  const auto sample_at = [&](int r, int c) {
    return data[static_cast<std::size_t>(r) *
                    static_cast<std::size_t>(row_stride_pixels) +
                static_cast<std::size_t>(c)];
  };

  std::vector<RgbPixel> out(static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height));
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      RgbPixel pixel;
      const int known_component = component_at(r, c);
      const double known_value = sample_at(r, c);

      for (int target = 0; target < 3; ++target) {
        double value = known_value;
        if (target != known_component) {
          double sum = 0.0;
          int count = 0;
          for (int dr = -1; dr <= 1; ++dr) {
            const int rr = r + dr;
            if (rr < 0 || rr >= height) continue;
            for (int dc = -1; dc <= 1; ++dc) {
              const int cc = c + dc;
              if (cc < 0 || cc >= width) continue;
              if (component_at(rr, cc) != target) continue;
              sum += sample_at(rr, cc);
              ++count;
            }
          }
          value = count > 0 ? sum / static_cast<double>(count) : 0.0;
        }

        if (target == 0) pixel.r = value;
        if (target == 1) pixel.g = value;
        if (target == 2) pixel.b = value;
      }

      out[static_cast<std::size_t>(r) * static_cast<std::size_t>(width) +
          static_cast<std::size_t>(c)] = pixel;
    }
  }
  return out;
}

std::array<ChannelStats, 3> rgb_image_stats(const std::vector<RgbPixel>& image) {
  std::array<ChannelStats, 3> out;
  out[0].label = "R";
  out[1].label = "G";
  out[2].label = "B";

  for (int component = 0; component < 3; ++component) {
    ChannelStats& s = out[static_cast<std::size_t>(component)];
    s.count = image.size();
    if (image.empty()) continue;

    // Welford online mean/M2: avoids the catastrophic cancellation of
    // sumsq/n - mean^2 when the DN level dwarfs the variance (e.g. a 12k-DN
    // green plane over tens of millions of pixels).
    double mean = 0.0;
    double m2 = 0.0;
    std::size_t k = 0;
    for (const RgbPixel& p : image) {
      const double v = component_value(p, component);
      ++k;
      const double delta = v - mean;
      mean += delta / static_cast<double>(k);
      m2 += delta * (v - mean);
      if (k == 1) {
        s.min = s.max = v;
      } else {
        if (v < s.min) s.min = v;
        if (v > s.max) s.max = v;
      }
    }

    s.mean = mean;
    const double var = m2 / static_cast<double>(image.size());
    s.stddev = var > 0.0 ? std::sqrt(var) : 0.0;
  }
  return out;
}

}  // namespace camera_iq
