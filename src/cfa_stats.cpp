#include "camera_iq/cfa_stats.hpp"

#include <cmath>

namespace camera_iq {

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
    const std::array<double, 4>& black_at_position, double white) {
  const auto labels = channel_labels(cdesc, color_at_position);

  struct Acc {
    double sum = 0.0, sumsq = 0.0, mn = 0.0, mx = 0.0;
    std::size_t n = 0, sat = 0;
    bool init = false;
  };
  std::array<Acc, 4> acc;

  if (data != nullptr && width > 0 && height > 0) {
    for (int r = 0; r < height; ++r) {
      const int row_base = (r & 1) * 2;  // (r % 2) * 2
      const std::size_t row_off = static_cast<std::size_t>(r) *
                                  static_cast<std::size_t>(width);
      for (int c = 0; c < width; ++c) {
        const std::size_t pos =
            static_cast<std::size_t>(row_base + (c & 1));  // 0..3
        const double raw = static_cast<double>(data[row_off +
                                                    static_cast<std::size_t>(c)]);
        double v = raw - black_at_position[pos];
        if (v < 0.0) v = 0.0;
        Acc& a = acc[pos];
        a.sum += v;
        a.sumsq += v * v;
        ++a.n;
        if (!a.init) {
          a.mn = a.mx = v;
          a.init = true;
        } else {
          if (v < a.mn) a.mn = v;
          if (v > a.mx) a.mx = v;
        }
        if (raw >= white) ++a.sat;
      }
    }
  }

  std::array<ChannelStats, 4> out;
  for (int p = 0; p < 4; ++p) {
    ChannelStats& s = out[static_cast<std::size_t>(p)];
    const Acc& a = acc[static_cast<std::size_t>(p)];
    s.label = labels[static_cast<std::size_t>(p)];
    s.count = a.n;
    if (a.n > 0) {
      const double n = static_cast<double>(a.n);
      s.mean = a.sum / n;
      const double var = a.sumsq / n - s.mean * s.mean;
      s.stddev = var > 0.0 ? std::sqrt(var) : 0.0;
      s.min = a.mn;
      s.max = a.mx;
      s.saturated_fraction = static_cast<double>(a.sat) / n;
    }
  }
  return out;
}

}  // namespace camera_iq
