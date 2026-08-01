#include "camera_iq/cfa_stats.hpp"

#include <cmath>
#include <limits>

namespace camera_iq {

std::optional<double> near_ceiling_threshold(double white, double black,
                                             double level) {
  if (!std::isfinite(level) || level <= 0.0 || level > 1.0) return std::nullopt;
  if (!std::isfinite(white) || !std::isfinite(black) || white <= black) {
    return std::nullopt;
  }
  return level * (white - black);
}

std::array<std::string, 4> channel_labels(
    const std::string& cdesc, const std::array<int, 4>& color_at_position) {
  std::array<char, 4> letters{'?', '?', '?', '?'};
  for (int p = 0; p < 4; ++p) {
    const int idx = color_at_position[static_cast<std::size_t>(p)];
    if (idx >= 0 && static_cast<std::size_t>(idx) < cdesc.size()) {
      letters[static_cast<std::size_t>(p)] = cdesc[static_cast<std::size_t>(idx)];
    }
  }
  std::array<std::string, 4> out;
  for (int p = 0; p < 4; ++p) {
    const char letter = letters[static_cast<std::size_t>(p)];
    int total = 0;
    for (char other : letters) {
      if (other == letter) ++total;
    }
    if (total <= 1) {
      out[static_cast<std::size_t>(p)] = std::string(1, letter);
    } else {
      int ordinal = 0;
      for (int q = 0; q <= p; ++q) {
        if (letters[static_cast<std::size_t>(q)] == letter) ++ordinal;
      }
      out[static_cast<std::size_t>(p)] =
          std::string(1, letter) + std::to_string(ordinal);
    }
  }
  return out;
}

std::array<ChannelStats, 4> cfa_plane_stats(
    const std::uint16_t* data, int width, int height,
    const std::array<int, 4>& color_at_position, const std::string& cdesc,
    const std::array<double, 4>& black_at_position, double white,
    double near_ceiling_level) {
  return cfa_plane_stats_strided(data, width, height, width, color_at_position,
                                 cdesc, black_at_position, white,
                                 near_ceiling_level);
}

std::array<ChannelStats, 4> cfa_plane_stats_strided(
    const std::uint16_t* data, int width, int height, int row_stride_pixels,
    const std::array<int, 4>& color_at_position, const std::string& cdesc,
    const std::array<double, 4>& black_at_position, double white,
    double near_ceiling_level) {
  const auto labels = channel_labels(cdesc, color_at_position);

  // Signal-referred near-ceiling threshold per CFA position. Resolved once so
  // the inner loop stays a comparison against a plain double.
  std::array<std::optional<double>, 4> near_ceiling{};
  for (int p = 0; p < 4; ++p) {
    near_ceiling[static_cast<std::size_t>(p)] = near_ceiling_threshold(
        white, black_at_position[static_cast<std::size_t>(p)],
        near_ceiling_level);
  }

  // Welford online mean/M2 per CFA position. For <=14-bit raw the sumsq/n -
  // mean^2 form is already exact (sums stay under 2^53), so this is a no-op
  // there; it earns its keep for 16-bit-class raw where a bright, near-flat
  // plane over millions of pixels would otherwise lose the variance to
  // cancellation.
  struct Acc {
    double mean = 0.0, m2 = 0.0, mn = 0.0, mx = 0.0;
    std::size_t n = 0, below_black = 0, sat = 0, near_ceiling = 0;
    bool init = false;
  };
  std::array<Acc, 4> acc;

  if (data != nullptr && width > 0 && height > 0 &&
      row_stride_pixels >= width) {
    for (int r = 0; r < height; ++r) {
      const int row_base = (r & 1) * 2;  // (r % 2) * 2
      const std::size_t row_off = static_cast<std::size_t>(r) *
                                  static_cast<std::size_t>(row_stride_pixels);
      for (int c = 0; c < width; ++c) {
        const std::size_t pos =
            static_cast<std::size_t>(row_base + (c & 1));  // 0..3
        const double raw = static_cast<double>(data[row_off +
                                                    static_cast<std::size_t>(c)]);
        double v = raw - black_at_position[pos];
        Acc& a = acc[pos];
        ++a.n;
        const double delta = v - a.mean;
        a.mean += delta / static_cast<double>(a.n);
        a.m2 += delta * (v - a.mean);
        if (v < 0.0) ++a.below_black;
        if (!a.init) {
          a.mn = a.mx = v;
          a.init = true;
        } else {
          if (v < a.mn) a.mn = v;
          if (v > a.mx) a.mx = v;
        }
        if (raw >= white) ++a.sat;
        const auto& threshold = near_ceiling[pos];
        if (threshold && v >= *threshold) ++a.near_ceiling;
      }
    }
  }

  std::array<ChannelStats, 4> out;
  for (int p = 0; p < 4; ++p) {
    ChannelStats& s = out[static_cast<std::size_t>(p)];
    const Acc& a = acc[static_cast<std::size_t>(p)];
    s.label = labels[static_cast<std::size_t>(p)];
    s.count = a.n;
    const bool threshold_valid =
        near_ceiling[static_cast<std::size_t>(p)].has_value();
    s.near_ceiling_fraction =
        threshold_valid ? 0.0 : std::numeric_limits<double>::quiet_NaN();
    if (a.n > 0) {
      const double n = static_cast<double>(a.n);
      s.mean = a.mean;
      const double var = a.m2 / n;
      s.stddev = var > 0.0 ? std::sqrt(var) : 0.0;
      s.min = a.mn;
      s.max = a.mx;
      s.below_black_fraction = static_cast<double>(a.below_black) / n;
      s.saturated_fraction = static_cast<double>(a.sat) / n;
      if (threshold_valid) {
        s.near_ceiling_fraction = static_cast<double>(a.near_ceiling) / n;
      }
    }
  }
  return out;
}

}  // namespace camera_iq
