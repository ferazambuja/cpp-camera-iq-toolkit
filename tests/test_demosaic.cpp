#include "camera_iq/commands.hpp"
#include "camera_iq/demosaic.hpp"

#include <array>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::RgbPixel;
using camera_iq::cmd_demosaic;
using camera_iq::demosaic_bilinear;
using camera_iq::rgb_image_stats;
using test::check;
using test::check_near;

namespace {

const std::array<int, 4> kRGGB = {0, 1, 3, 2};
const std::array<int, 4> kBGGR = {2, 3, 1, 0};
const std::string kCdesc = "RGBG";

void check_pixel(const std::vector<RgbPixel>& img, int width, int r, int c,
                 double red, double green, double blue,
                 const std::string& name) {
  const RgbPixel& p = img[static_cast<std::size_t>(r * width + c)];
  check_near(p.r, red, 1e-9, name + " R");
  check_near(p.g, green, 1e-9, name + " G");
  check_near(p.b, blue, 1e-9, name + " B");
}

}  // namespace

void TESTS() {
  // Constant per-channel fields must reconstruct to constant RGB everywhere.
  {
    const int width = 4;
    const int height = 4;
    std::vector<double> cfa = {
        10, 20, 10, 20,
        20, 30, 20, 30,
        10, 20, 10, 20,
        20, 30, 20, 30,
    };
    const auto rgb =
        demosaic_bilinear(cfa.data(), width, height, width, kRGGB, kCdesc);
    check(rgb.size() == cfa.size(), "constant: output pixel count");
    check_pixel(rgb, width, 0, 0, 10, 20, 30, "constant top-left");
    check_pixel(rgb, width, 1, 1, 10, 20, 30, "constant blue-site");
    check_pixel(rgb, width, 3, 3, 10, 20, 30, "constant bottom-right edge");
  }

  // Hand-computed center interpolation on a non-constant 5x5 RGGB mosaic.
  {
    const int width = 5;
    const int height = 5;
    std::vector<double> cfa = {
        10, 101, 20, 102, 30,
        103, 200, 104, 220, 105,
        40, 106, 50, 107, 60,
        108, 240, 109, 260, 110,
        70, 111, 80, 112, 90,
    };
    const auto rgb =
        demosaic_bilinear(cfa.data(), width, height, width, kRGGB, kCdesc);
    check_pixel(rgb, width, 2, 2, 50, 106.5, 230, "center red site");
    check_pixel(rgb, width, 2, 1, 45, 106, 220, "center green-on-red-row site");
    check_pixel(rgb, width, 1, 2, 35, 104, 210, "center green-on-blue-row site");
    check_pixel(rgb, width, 1, 1, 30, 103.5, 200, "center blue site");
  }

  // Edge handling averages only same-color samples that exist inside bounds.
  {
    const int width = 3;
    const int height = 3;
    std::vector<double> cfa = {
        10, 20, 30,
        40, 50, 60,
        70, 80, 90,
    };
    const auto rgb =
        demosaic_bilinear(cfa.data(), width, height, width, kRGGB, kCdesc);
    check_pixel(rgb, width, 0, 0, 10, 30, 50, "edge red corner");
    check_pixel(rgb, width, 0, 1, 20, 20, 50, "edge green top");
    check_pixel(rgb, width, 2, 2, 90, 70, 50, "edge red opposite corner");
  }

  // Same algorithm must follow CFA phase, not assume RGGB.
  {
    const int width = 4;
    const int height = 4;
    std::vector<double> cfa = {
        30, 20, 30, 20,
        20, 10, 20, 10,
        30, 20, 30, 20,
        20, 10, 20, 10,
    };
    const auto rgb =
        demosaic_bilinear(cfa.data(), width, height, width, kBGGR, kCdesc);
    check_pixel(rgb, width, 0, 0, 10, 20, 30, "phase BGGR top-left");
    check_pixel(rgb, width, 1, 1, 10, 20, 30, "phase BGGR red-site");
  }

  {
    const auto rgb = demosaic_bilinear(nullptr, 2, 2, 2, kRGGB, kCdesc);
    check(rgb.empty(), "null input returns empty output");
  }

  {
    std::vector<RgbPixel> rgb = {
        {1, 10, 100},
        {3, 14, 108},
        {5, 18, 116},
    };
    const auto stats = rgb_image_stats(rgb);
    check(stats[0].label == "R" && stats[1].label == "G" &&
              stats[2].label == "B",
          "stats: labels");
    check(stats[0].count == 3 && stats[1].count == 3 && stats[2].count == 3,
          "stats: counts");
    check_near(stats[0].mean, 3, 1e-9, "stats: R mean");
    check_near(stats[1].mean, 14, 1e-9, "stats: G mean");
    check_near(stats[2].stddev, 6.531972647421808, 1e-12, "stats: B stddev");
  }

  {
    std::vector<RgbPixel> rgb = {
        {12000, 15000, 9000},
        {12001, 15001, 9001},
    };
    const auto stats = rgb_image_stats(rgb);
    check_near(stats[0].mean, 12000.5, 1e-12, "stats stable: R mean");
    check_near(stats[0].stddev, 0.5, 1e-12, "stats stable: R stddev");
    check_near(stats[1].stddev, 0.5, 1e-12, "stats stable: G stddev");
    check_near(stats[2].stddev, 0.5, 1e-12, "stats stable: B stddev");
  }

  check(cmd_demosaic(0, nullptr) == 2, "cli: demosaic requires raw file");
}
