#include <libraw/libraw.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "camera_iq/demosaic.hpp"
#include "camera_iq/raw_meta.hpp"

namespace {

class ProbeRaw : public LibRaw {
 public:
  using LibRaw::lin_interpolate;
  using LibRaw::pre_interpolate;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: libraw_bilinear_compare <raw-file>\n";
    return 2;
  }

  const std::string path = argv[1];
  const auto cfa = camera_iq::read_raw_cfa_image(path);
  if (!cfa) {
    std::cerr << "cannot read CFA image: " << path << "\n";
    return 1;
  }
  const auto ours =
      camera_iq::demosaic_bilinear(cfa->samples.data(), cfa->width, cfa->height,
                                   cfa->row_stride_pixels,
                                   cfa->color_at_position, cfa->cdesc);
  if (ours.empty()) {
    std::cerr << "camera_iq demosaic returned empty image\n";
    return 1;
  }

  ProbeRaw raw;
  if (raw.open_file(path.c_str()) != LIBRAW_SUCCESS) {
    std::cerr << "LibRaw open failed: " << path << "\n";
    return 1;
  }
  if (raw.unpack() != LIBRAW_SUCCESS) {
    std::cerr << "LibRaw unpack failed: " << path << "\n";
    return 1;
  }
  if (raw.raw2image_ex(1) != LIBRAW_SUCCESS) {
    std::cerr << "LibRaw raw2image_ex failed: " << path << "\n";
    return 1;
  }
  raw.pre_interpolate();
  raw.lin_interpolate();
  if (raw.imgdata.image == nullptr) {
    std::cerr << "LibRaw produced no image\n";
    return 1;
  }

  const auto& sizes = raw.imgdata.sizes;
  const int width = std::min(cfa->width, static_cast<int>(sizes.iwidth));
  const int height = std::min(cfa->height, static_cast<int>(sizes.iheight));
  if (width <= 0 || height <= 0) {
    std::cerr << "invalid comparison dimensions\n";
    return 1;
  }

  double sum_abs[3] = {0.0, 0.0, 0.0};
  double max_abs[3] = {0.0, 0.0, 0.0};
  std::size_t count = 0;
  for (int r = 0; r < height; ++r) {
    for (int c = 0; c < width; ++c) {
      const std::size_t libraw_offset =
          static_cast<std::size_t>(r) * static_cast<std::size_t>(sizes.iwidth) +
          static_cast<std::size_t>(c);
      const std::size_t ours_offset =
          static_cast<std::size_t>(r) * static_cast<std::size_t>(cfa->width) +
          static_cast<std::size_t>(c);
      const double libraw_rgb[3] = {
          static_cast<double>(raw.imgdata.image[libraw_offset][0]),
          static_cast<double>(raw.imgdata.image[libraw_offset][1]),
          static_cast<double>(raw.imgdata.image[libraw_offset][2])};
      // LibRaw's image buffer is unsigned. Clip our signed residuals only for
      // this comparison; the production demosaic summary preserves negatives.
      const double ours_rgb[3] = {std::max(0.0, ours[ours_offset].r),
                                  std::max(0.0, ours[ours_offset].g),
                                  std::max(0.0, ours[ours_offset].b)};
      for (int ch = 0; ch < 3; ++ch) {
        const double diff = std::abs(ours_rgb[ch] - libraw_rgb[ch]);
        sum_abs[ch] += diff;
        if (diff > max_abs[ch]) max_abs[ch] = diff;
      }
      ++count;
    }
  }

  std::cout << "file=" << path << "\n";
  std::cout << "camera_iq_dimensions=" << cfa->width << "x" << cfa->height
            << "\n";
  std::cout << "libraw_dimensions=" << sizes.iwidth << "x" << sizes.iheight
            << "\n";
  std::cout << "pixels_compared=" << count << "\n";
  std::cout << "mean_abs_diff_rgb=" << (sum_abs[0] / count) << ","
            << (sum_abs[1] / count) << "," << (sum_abs[2] / count) << "\n";
  std::cout << "max_abs_diff_rgb=" << max_abs[0] << "," << max_abs[1] << ","
            << max_abs[2] << "\n";
  return 0;
}
