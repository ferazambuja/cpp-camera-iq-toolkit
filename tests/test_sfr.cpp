#include "camera_iq/sfr.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "camera_iq/raw_meta.hpp"
#include "camera_iq/roi.hpp"
#include "harness.hpp"

namespace {

using camera_iq::RawCfaImage;
using camera_iq::RoiRect;

double erf_edge(double distance, double sigma) {
  return 0.5 * (1.0 + std::erf(distance / (std::sqrt(2.0) * sigma)));
}

RawCfaImage synthetic_green_edge(int width, int height, double angle_deg,
                                 double sigma, bool horizontal) {
  RawCfaImage image;
  image.width = width;
  image.height = height;
  image.row_stride_pixels = width;
  image.cdesc = "RGBG";
  image.color_at_position = {0, 1, 1, 2};
  image.meta.white_level = 4095.0;
  image.samples.assign(static_cast<std::size_t>(width * height), 0.0);

  const double slope = std::tan(angle_deg * std::numbers::pi / 180.0);
  const double cx = 0.5 * static_cast<double>(width - 1);
  const double cy = 0.5 * static_cast<double>(height - 1);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      double distance = 0.0;
      if (horizontal) {
        const double edge_y = cy + slope * (static_cast<double>(x) - cx);
        distance = (static_cast<double>(y) - edge_y) /
                   std::sqrt(1.0 + slope * slope);
      } else {
        const double edge_x = cx + slope * (static_cast<double>(y) - cy);
        distance = (static_cast<double>(x) - edge_x) /
                   std::sqrt(1.0 + slope * slope);
      }
      image.samples[static_cast<std::size_t>(y * width + x)] =
          1000.0 * erf_edge(distance, sigma);
    }
  }
  return image;
}

RawCfaImage flat_image(int width, int height) {
  RawCfaImage image;
  image.width = width;
  image.height = height;
  image.row_stride_pixels = width;
  image.cdesc = "RGBG";
  image.color_at_position = {0, 1, 1, 2};
  image.meta.white_level = 4095.0;
  image.samples.assign(static_cast<std::size_t>(width * height), 100.0);
  return image;
}

std::filesystem::path temp_file(const std::string& name) {
  return std::filesystem::temp_directory_path() / name;
}

}  // namespace

void TESTS() {
  {
    const auto mag = camera_iq::dft_magnitude({1.0, 0.0, 0.0, 0.0});
    test::check(mag.size() == 3, "DFT returns non-redundant bins");
    for (double v : mag) {
      test::check_near(v, 1.0, 1e-12, "delta DFT is flat");
    }
  }

  {
    constexpr int n = 32;
    constexpr int bin = 3;
    std::vector<double> signal;
    signal.reserve(n);
    for (int i = 0; i < n; ++i) {
      signal.push_back(std::cos(2.0 * std::numbers::pi * bin *
                                static_cast<double>(i) / n));
    }
    const auto mag = camera_iq::dft_magnitude(signal);
    double max_other = 0.0;
    for (std::size_t i = 1; i < mag.size(); ++i) {
      if (static_cast<int>(i) != bin) max_other = std::max(max_other, mag[i]);
    }
    test::check(mag[bin] > 15.9, "cosine DFT peak lands at expected bin");
    test::check(max_other < 1e-10, "cosine DFT rejects wrong bins");
  }

  {
    constexpr double delta = 0.25;
    test::check_near(camera_iq::adjacent_difference_response(0.25, delta),
                     0.993587, 1e-6,
                     "adjacent-difference response at 0.25 cy/px");
    test::check_near(camera_iq::adjacent_difference_response(0.5, delta),
                     0.974495, 1e-6,
                     "adjacent-difference response at 0.5 cy/px");
    test::check_near(camera_iq::adjacent_difference_response(1.0, delta),
                     0.900316, 1e-6,
                     "adjacent-difference response at 1.0 cy/px");
  }

  {
    const double sigma = 1.25;
    const auto image = synthetic_green_edge(160, 144, -6.0, sigma, true);
    const auto result =
        camera_iq::analyze_green_sfr(image, RoiRect{20, 16, 120, 112});
    test::check(result.accepted, "synthetic horizontal green edge accepted");
    test::check(result.orientation == "horizontal",
                "horizontal edge orientation detected");
    test::check_near(result.edge_angle_deg, -6.0, 0.08,
                     "synthetic horizontal edge angle recovered");
    test::check_near(result.mtf50_cy_per_px, 0.18739 / sigma, 0.018,
                     "Gaussian point-sampled edge MTF50 recovered");
    test::check_near(result.mtf_at_nyquist, std::exp(-2.0 *
                         std::numbers::pi * std::numbers::pi * sigma * sigma *
                         0.5 * 0.5), 0.08,
                     "Gaussian point-sampled edge Nyquist MTF recovered");
  }

  {
    const auto image = synthetic_green_edge(144, 160, 5.5, 1.4, false);
    const auto result =
        camera_iq::analyze_green_sfr(image, RoiRect{16, 20, 112, 120});
    test::check(result.accepted, "synthetic vertical green edge accepted");
    test::check(result.orientation == "vertical",
                "vertical edge orientation detected");
    test::check_near(result.edge_angle_deg, 5.5, 0.08,
                     "synthetic vertical edge angle recovered");
  }

  {
    const auto result =
        camera_iq::analyze_green_sfr(flat_image(64, 64), RoiRect{0, 0, 64, 64});
    test::check(!result.accepted, "flat ROI rejected");
    test::check(result.rejection_reason == "low_contrast",
                "flat ROI rejected with low_contrast");
  }

  {
    const auto image = synthetic_green_edge(96, 96, 0.4, 1.0, true);
    const auto result =
        camera_iq::analyze_green_sfr(image, RoiRect{8, 8, 80, 80});
    test::check(!result.accepted, "too-shallow edge rejected");
    test::check(result.rejection_reason == "edge_angle_out_of_range",
                "too-shallow edge rejection reason pinned");
  }

  {
    const auto path = temp_file("camera_iq_test_y_multi.csv");
    {
      std::ofstream os(path);
      os << "Imatest,4.5.7, , SFRplus\n";
      os << "File,NIKON D810_50mm_f5.6_.NEF\n";
      os << "Run date,10-Dec-2016 13:04:04,,Build 2016-11-22\n\n";
      os << "N,Distance %,Direction,X1,Y1,X2,Y2,Width,Height,Region,Edge ID\n";
      os << "1,  2.6,AL,3483,2231,3699,2568,217,338,Center,0_0_R\n";
      os << "2, 77.0,AL,463,906,679,1237,217,332,Corner,-4_-2_L_C\n\n";
      os << "N,MTF50 (Cy/Pxl),R1090 (pxl),CA area(pxl),MTF50 (LW/PH),"
            "R1090 (/PH),Peak MTF,MTF50P (Cy/Pxl)\n";
      os << "1, 0.2400, 2.3112, 0.000, 2365.3, 2132.2, 1.0009, 0.2399\n";
      os << "2, 0.1750, 3.0, 0.000, 1700, 2000, 1.0, 0.1740\n";
    }
    const auto oracle = camera_iq::read_imatest_y_multi(path);
    test::check(oracle.has_value(), "Imatest Y_multi parser accepts fixture");
    test::check(oracle->filename == "NIKON D810_50mm_f5.6_.NEF",
                "Imatest filename parsed exactly");
    test::check_near(oracle->run_date == "10-Dec-2016 13:04:04" ? 1.0 : 0.0,
                     1.0, 0.0, "Imatest run date parsed");
    test::check(oracle->center_roi_full_frame.x == 3483 &&
                    oracle->center_roi_full_frame.y == 2231 &&
                    oracle->center_roi_full_frame.width == 217 &&
                    oracle->center_roi_full_frame.height == 338,
                "Imatest center ROI parsed from row N=1");
    test::check_near(oracle->center_mtf50_cy_per_px, 0.2400, 1e-12,
                     "Imatest center MTF50 parsed from matching row N=1");
    test::check_near(oracle->center_mtf50p_cy_per_px, 0.2399, 1e-12,
                     "Imatest center MTF50P parsed from matching row N=1");
    std::filesystem::remove(path);
  }

  {
    std::vector<camera_iq::SfrSweepPoint> sweep = {
        {1.4, 0.1158}, {1.8, 0.0899}, {2.0, 0.1121},
        {2.8, 0.1707}, {4.0, 0.1949}, {5.6, 0.2400},
        {8.0, 0.2388}, {11.0, 0.1989}, {16.0, 0.1735}};
    const auto gate = camera_iq::evaluate_aperture_trend(sweep);
    test::check(gate.passed, "coherent D810 oracle trend passes");
    test::check_near(gate.mid_plateau_min, 0.1949, 1e-12,
                     "trend gate plateau min pinned");
    test::check_near(gate.wide_open_max, 0.1158, 1e-12,
                     "trend gate wide-open max pinned");
    test::check_near(gate.f16_value, 0.1735, 1e-12,
                     "trend gate f16 value pinned");
    test::check_near(gate.argmax_aperture, 5.6, 1e-12,
                     "trend gate argmax aperture pinned");
  }
}
