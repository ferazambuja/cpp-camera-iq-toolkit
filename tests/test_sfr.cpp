#include "camera_iq/sfr.hpp"

#include <cmath>
#include <array>
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

// Area-integrated hard step: each pixel's value approximates the fraction of
// the unit pixel area on the bright side of the slanted edge (8x8 supersample).
// The pixel aperture makes the analytic MTF |sinc(f cos t)|*|sinc(f sin t)|
// along the edge normal, ~|sinc(f)| at ~6 deg, so MTF50 ~ 0.6034 cy/px --
// beyond the sensor Nyquist 0.5, measurable only on the 4x oversampled axis.
RawCfaImage synthetic_green_step_area(int width, int height, double angle_deg) {
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
  constexpr int kSub = 8;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int bright = 0;
      for (int sy = 0; sy < kSub; ++sy) {
        for (int sx = 0; sx < kSub; ++sx) {
          const double px = static_cast<double>(x) +
                            (static_cast<double>(sx) + 0.5) / kSub - 0.5;
          const double py = static_cast<double>(y) +
                            (static_cast<double>(sy) + 0.5) / kSub - 0.5;
          const double edge_x = cx + slope * (py - cy);
          if (px >= edge_x) ++bright;
        }
      }
      image.samples[static_cast<std::size_t>(y * width + x)] =
          1000.0 * static_cast<double>(bright) / (kSub * kSub);
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

struct FixtureRow {
  int n;
  const char* direction;
  const char* region;
  const char* edge_id;
  int x1;
  int y1;
  int width;
  int height;
  double mtf50;
  double mtf50p;
};

constexpr std::array<FixtureRow, 23> kFieldRows = {{
    {1, "AL", "Center", "0_0_R", 3483, 2231, 217, 338, 0.2400, 0.2399},
    {2, "AL", "Corner", "-4_-2_L_C", 463, 906, 217, 332, 0.1894, 0.1888},
    {3, "AR", "Pt Way", "4_-2_R_C", 6125, 949, 217, 334, 0.1562, 0.1562},
    {4, "L", "Corner", "-4_2_L_C", 389, 3536, 217, 332, 0.1750, 0.1742},
    {5, "BR", "Pt Way", "4_2_R_C", 6139, 3592, 217, 333, 0.1750, 0.1744},
    {6, "L", "Pt Way", "-2_-1_L", 1734, 1558, 217, 335, 0.2024, 0.2024},
    {7, "L", "Pt Way", "-2_1_L", 1706, 2888, 217, 335, 0.1975, 0.1972},
    {8, "AR", "Pt Way", "2_-1_R", 4821, 1581, 217, 337, 0.2221, 0.2221},
    {9, "BR", "Pt Way", "2_1_R", 4819, 2914, 217, 337, 0.2274, 0.2274},
    {10, "L", "Pt Way", "-4_0_L_E", 417, 2211, 217, 332, 0.1889, 0.1884},
    {11, "R", "Pt Way", "4_0_R_E", 6139, 2260, 217, 333, 0.1769, 0.1757},
    {12, "A", "Pt Way", "0_-2_R_E", 3498, 910, 217, 337, 0.2377, 0.2377},
    {13, "B", "Pt Way", "0_2_R_E", 3470, 3575, 217, 337, 0.2780, 0.2655},
    {14, "L", "Pt Way", "-4_-1_L_E", 437, 1555, 217, 332, 0.1934, 0.1924},
    {15, "R", "Pt Way", "4_-1_R_E", 6134, 1601, 217, 334, 0.1802, 0.1802},
    {16, "L", "Pt Way", "-4_1_L_E", 401, 2872, 217, 332, 0.1642, 0.1628},
    {17, "R", "Pt Way", "4_1_R_E", 6141, 2925, 217, 333, 0.1852, 0.1836},
    {18, "AL", "Pt Way", "-2_-2_L_E", 1752, 904, 217, 335, 0.1899, 0.1899},
    {19, "AR", "Pt Way", "2_-2_R_E", 4820, 926, 217, 336, 0.1939, 0.1939},
    {20, "BL", "Pt Way", "-2_2_L_E", 1697, 3558, 217, 336, 0.1927, 0.1927},
    {21, "BR", "Pt Way", "2_2_R_E", 4814, 3586, 217, 336, 0.2165, 0.2156},
    {22, "L", "Pt Way", "-2_0_L", 1718, 2220, 217, 336, 0.2196, 0.2194},
    {23, "R", "Pt Way", "2_0_R", 4821, 2245, 217, 336, 0.2390, 0.2388},
}};

void write_field_y_multi_fixture(const std::filesystem::path& path,
                                 bool duplicate_mtf_n = false,
                                 bool omit_mtf_n_23 = false,
                                 bool malformed_edge_id = false) {
  std::ofstream os(path);
  os << "Imatest,4.5.7, , SFRplus\n";
  os << "File,NIKON D810_50mm_f5.6_.NEF\n";
  os << "Run date,10-Dec-2016 13:04:04,,Build 2016-11-22\n\n";
  os << "N,Distance %,Direction,X1,Y1,X2,Y2,Width,Height,Region,Edge ID,"
        "X px from ctr,Y px from ctr,CSV summary file\n";
  for (const auto& row : kFieldRows) {
    const char* edge_id =
        malformed_edge_id && row.n == 3 ? "not_a_grid_edge" : row.edge_id;
    const auto csv_summary_file =
        std::filesystem::temp_directory_path() /
        ("NIKON D810_50mm_f5.6__Y" + std::string(row.direction) + " " +
         std::to_string(row.n) + "_MTF.csv");
    os << row.n << ",  2.6," << row.direction << "," << row.x1 << ","
       << row.y1 << "," << (row.x1 + row.width - 1) << ","
       << (row.y1 + row.height - 1) << "," << row.width << ","
       << row.height << "," << row.region << "," << edge_id << ","
       << (row.x1 - 3580.5) << "," << (row.y1 - 2295.5)
       << "," << csv_summary_file.generic_string() << "\n";
  }
  os << "\n";
  os << "N,MTF50 (Cy/Pxl),R1090 (pxl),CA area(pxl),MTF50 (LW/PH),"
        "R1090 (/PH),Peak MTF,MTF50P (Cy/Pxl)\n";
  for (const auto& row : kFieldRows) {
    if (omit_mtf_n_23 && row.n == 23) continue;
    const int n = duplicate_mtf_n && row.n == 23 ? 22 : row.n;
    os << n << "," << row.mtf50 << ",2.3,0.000,2000,2000,1.001,"
       << row.mtf50p << "\n";
  }
  os << "\n";
  os << "Summary,10-Dec-2016 13:04:04,,NIKON D810_50mm_f5.6_.NEF\n";
  os << "23,Regions,3,Center,18,Part way,2,Corner\n";
  os << " ,MTF50 (Cy/Pxl),R1090 (pxl),CA (pxl),MTF50 (LW/PH),"
        "R1090 (/PH),Peak MTF,MTF50P (Cy/Pxl),MTF50P (LW/PH),"
        "MTF20 (Cy/Pxl)\n";
  os << "Mean Ctr,0.2523,2.2040,0.0000,2486.8,2246.9,1.0040,0.2481,"
        "2445.0,0.4638\n";
  os << "Worst Cor,0.1750,2.7672,0.0000,1725.2,1796.7,1.0110,0.1742,"
        "1716.8,0.2916\n\n";
  os << "More results: C denotes standardized sharpening\n";
  os << "N,MTF20 (Cy/Pxl),MTF20 (LW/PH),LSF PW50 (pxl),Gamma from chart,"
        "R-G pixel shift,B-G pixel shift\n";
  for (const auto& row : kFieldRows) {
    os << row.n << "," << (row.n == 13 ? 0.9999 : 0.4000) << ",4000,1.5,"
       << "0.509,0.00,0.00\n";
  }
}

void write_center_compatible_y_multi_fixture(const std::filesystem::path& path) {
  std::ofstream os(path);
  os << "Imatest,4.5.7, , SFRplus\n";
  os << "File,NIKON D810_50mm_f5.6_.NEF\n";
  os << "Run date,10-Dec-2016 13:04:04,,Build 2016-11-22\n\n";
  os << "N,Distance %,Direction,X1,Y1,X2,Y2,Width,Height,Region,Edge ID\n";
  os << "1,  2.6,AL,3483,2231,3699,2568,217,338,Center,0_0_R\n";
  os << "2, 77.0,AL,463,906,679,1237,217,332,Corner,malformed\n\n";
  os << "N,MTF50 (Cy/Pxl),R1090 (pxl),CA area(pxl),MTF50 (LW/PH),"
        "R1090 (/PH),Peak MTF,MTF50P (Cy/Pxl)\n";
  os << "1, 0.2400, 2.3112, 0.000, 2365.3, 2132.2, 1.0009, 0.2399\n";
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
    // Area-integrated (pixel-aperture) step edge: analytic MTF ~ |sinc(f)|,
    // MTF50 ~ 0.6034 cy/px. Distinct sampling model from the point-sampled
    // Gaussian erf test above. Tolerance covers the known small biases:
    // 0.25 px bin-average aperture (~3.6% at 0.6 cy/px, uncorrected by
    // design), Hamming window broadening, and 8x8 supersample quantization.
    // Also the load-bearing frequency-axis check: a compact-green-grid (2x)
    // frequency error would read ~0.30 or ~1.21 and fail hard.
    const auto image = synthetic_green_step_area(160, 144, -6.0);
    const auto result =
        camera_iq::analyze_green_sfr(image, RoiRect{20, 16, 120, 112});
    test::check(result.accepted, "area-integrated step edge accepted");
    test::check_near(result.mtf50_cy_per_px, 0.6034, 0.03,
                     "area-integrated step MTF50 near pixel-aperture limit");
    test::check(result.mtf50_cy_per_px > 0.5,
                "area-integrated step MTF50 beyond sensor Nyquist");
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
    const auto path = temp_file("camera_iq_test_y_multi_center_compat.csv");
    write_center_compatible_y_multi_fixture(path);
    const auto oracle = camera_iq::read_imatest_y_multi(path);
    test::check(oracle.has_value(),
                "Imatest center adapter ignores malformed non-center rows");
    test::check_near(oracle->center_mtf50_cy_per_px, 0.2400, 1e-12,
                     "Imatest center adapter preserves old center behavior");
    std::filesystem::remove(path);
  }

  {
    const auto path = temp_file("camera_iq_test_y_multi_field.csv");
    write_field_y_multi_fixture(path);
    const auto oracle = camera_iq::read_imatest_y_multi_file(path);
    test::check(oracle.has_value(),
                "Imatest Y_multi all-row parser accepts 23-row fixture");
    test::check(oracle->filename == "NIKON D810_50mm_f5.6_.NEF",
                "Imatest all-row parser preserves filename");
    test::check(oracle->rois.size() == 23,
                "Imatest all-row parser preserves all 23 ROI rows");
    test::check(oracle->rois.front().n == 1 &&
                    oracle->rois.front().edge_id == "0_0_R",
                "Imatest all-row parser preserves center row");
    const auto& right_corner = oracle->rois[2];
    test::check(right_corner.n == 3 && right_corner.region_label == "Pt Way",
                "Imatest fixture pins right corner Region label trap");
    test::check(right_corner.edge_id_grid_x == 4 &&
                    right_corner.edge_id_grid_y == -2 &&
                    right_corner.edge_id_suffix == "C" &&
                    right_corner.physical_corner,
                "Imatest parser derives physical corner from edge ID suffix");
    test::check(oracle->rois.back().n == 23 &&
                    oracle->rois.back().edge_id == "2_0_R",
                "Imatest all-row parser reaches row 23");
    test::check_near(oracle->rois[12].imatest_mtf50_cy_per_px, 0.2780,
                     1e-12,
                     "Imatest parser ignores trailing MTF20 table");
    test::check(
        oracle->rois[0].csv_summary_file ==
            "NIKON D810_50mm_f5.6__YAL 1_MTF.csv",
        "Imatest parser preserves CSV summary file basename");
    test::check(oracle->rois[0].csv_summary_file.find('/') ==
                    std::string::npos,
                "Imatest parser strips CSV summary file directory");
    test::check(oracle->rois[0].full_frame_roi.width == 217 &&
                    oracle->rois[0].full_frame_roi.height == 338,
                "Imatest parser validates inclusive X2/Y2 geometry");
    const auto summary = camera_iq::summarize_imatest_field_mtf(*oracle);
    test::check(summary.has_value(),
                "Imatest field summary accepts complete field file");
    test::check(summary->physical_corner_count == 4,
                "Imatest field summary counts physical corners by edge ID");
    test::check(summary->field_argmax_n == 13,
                "Imatest field summary records non-center argmax");
    test::check(!summary->center_is_field_max,
                "Imatest field summary does not assume center is field max");
    test::check_near(summary->center_mtf50_cy_per_px, 0.2400, 1e-12,
                     "Imatest field summary pins center MTF50");
    test::check_near(summary->physical_corner_max_mtf50_cy_per_px, 0.1894,
                     1e-12, "Imatest field summary pins corner max");
    test::check(summary->center_above_physical_corner_max,
                "Imatest field summary center-corner gate passes for f/5.6");
    std::filesystem::remove(path);
  }

  {
    auto make_file = [](double center, double corner_max) {
      camera_iq::ImatestYMultiFile file;
      file.filename = "fixture.NEF";
      file.run_date = "fixture";
      file.rois.push_back(camera_iq::ImatestYMultiRoi{
          .n = 1, .edge_id = "0_0_R", .imatest_mtf50_cy_per_px = center});
      for (int n = 2; n <= 5; ++n) {
        camera_iq::ImatestYMultiRoi roi;
        roi.n = n;
        roi.edge_id = n == 2   ? "-4_-2_L_C"
                      : n == 3 ? "4_-2_R_C"
                      : n == 4 ? "-4_2_L_C"
                               : "4_2_R_C";
        roi.physical_corner = true;
        roi.imatest_mtf50_cy_per_px = n == 2 ? corner_max : corner_max - 0.01;
        file.rois.push_back(roi);
      }
      return file;
    };
    const auto f4 = camera_iq::summarize_imatest_field_mtf(
        make_file(0.1949, 0.1959));
    const auto f56 = camera_iq::summarize_imatest_field_mtf(
        make_file(0.2400, 0.1894));
    const auto f8 = camera_iq::summarize_imatest_field_mtf(
        make_file(0.2388, 0.1753));
    const auto f11 = camera_iq::summarize_imatest_field_mtf(
        make_file(0.1989, 0.1635));
    test::check(f4 && !f4->center_above_physical_corner_max,
                "field corner summary keeps f/4 diagnostic-only near tie");
    test::check(f56 && f56->center_above_physical_corner_max,
                "field corner summary passes f/5.6 oracle gate");
    test::check(f8 && f8->center_above_physical_corner_max,
                "field corner summary passes f/8 oracle gate");
    test::check(f11 && f11->center_above_physical_corner_max,
                "field corner summary passes f/11 oracle gate");
  }

  {
    const auto path = temp_file("camera_iq_test_y_multi_bad_edge.csv");
    write_field_y_multi_fixture(path, false, false, true);
    const auto oracle = camera_iq::read_imatest_y_multi_file(path);
    test::check(!oracle, "Imatest all-row parser rejects malformed edge ID");
    std::filesystem::remove(path);
  }

  {
    const auto path = temp_file("camera_iq_test_y_multi_missing_mtf.csv");
    write_field_y_multi_fixture(path, false, true);
    const auto oracle = camera_iq::read_imatest_y_multi_file(path);
    test::check(!oracle, "Imatest all-row parser rejects missing MTF row");
    std::filesystem::remove(path);
  }

  {
    const auto path = temp_file("camera_iq_test_y_multi_duplicate_mtf.csv");
    write_field_y_multi_fixture(path, true, false);
    const auto oracle = camera_iq::read_imatest_y_multi_file(path);
    test::check(!oracle, "Imatest all-row parser rejects duplicate MTF row");
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
