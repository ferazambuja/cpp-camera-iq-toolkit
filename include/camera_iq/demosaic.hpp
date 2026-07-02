#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "camera_iq/cfa_stats.hpp"

namespace camera_iq {

struct RgbPixel {
  double r = 0.0;
  double g = 0.0;
  double b = 0.0;
};

// Hand-written bilinear demosaic over a black-subtracted active Bayer mosaic.
//
// `data` points at the active-area origin. `row_stride_pixels` may exceed
// `width` when walking a cropped view of a larger buffer. `color_at_position`
// contains LibRaw COLOR() indices for (0,0)(0,1)(1,0)(1,1), and `cdesc` maps
// those indices to channel letters. Only RGB Bayer components are reconstructed;
// unsupported descriptors return an empty image.
std::vector<RgbPixel> demosaic_bilinear(
    const double* data, int width, int height, int row_stride_pixels,
    const std::array<int, 4>& color_at_position, std::string_view cdesc);

// Per-channel summary statistics for an RGB image. Labels are R, G, B.
std::array<ChannelStats, 3> rgb_image_stats(const std::vector<RgbPixel>& image);

}  // namespace camera_iq
