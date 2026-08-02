#include "camera_iq/roi.hpp"
#include "camera_iq/raw_meta.hpp"

#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::RawCfaImage;
using camera_iq::RoiRect;
using camera_iq::cfa_balanced_roi;
using camera_iq::centered_cfa_balanced_roi;
using camera_iq::parse_roi_spec;
using camera_iq::raw_cfa_report_for_roi;
using test::check;
using test::check_near;

namespace {

RawCfaImage fixture_image() {
  RawCfaImage img;
  img.width = 6;
  img.height = 4;
  img.row_stride_pixels = 6;
  img.color_at_position = {0, 1, 3, 2};  // RGGB with cdesc RGBG.
  img.cdesc = "RGBG";
  img.meta.black_per_channel = {100, 101, 102, 103};
  img.meta.black_level = 101.5;
  img.meta.white_level = 1000;
  img.samples.assign(static_cast<std::size_t>(img.width * img.height), 9999.0);

  // ROI x=2,y=0,w=4,h=4. Values outside this rectangle must be ignored.
  const auto set = [&](int r, int c, double value) {
    img.samples[static_cast<std::size_t>(r * img.row_stride_pixels + c)] = value;
  };

  // Position 0 (R): four samples, mean 25.
  set(0, 2, 10);
  set(0, 4, 20);
  set(2, 2, 30);
  set(2, 4, 40);

  // Position 1 (G1): one below black residual.
  set(0, 3, -5);
  set(0, 5, 5);
  set(2, 3, 15);
  set(2, 5, 25);

  // Position 2 (G2): flat field.
  set(1, 2, 100);
  set(1, 4, 100);
  set(3, 2, 100);
  set(3, 4, 100);

  // Position 3 (B): two saturated raw values because residual + black >= white.
  set(1, 3, 100);
  set(1, 5, 897);
  set(3, 3, 897);
  set(3, 5, 0);

  return img;
}

}  // namespace

void TESTS() {
  {
    const auto roi = parse_roi_spec("3,5,9,7");
    check(roi.has_value(), "parse roi: accepts x,y,width,height");
    if (roi) {
      check(roi->x == 3 && roi->y == 5 && roi->width == 9 &&
                roi->height == 7,
            "parse roi: fields assigned");
    }
    check(!parse_roi_spec("3,5,9"), "parse roi: rejects too few fields");
    check(!parse_roi_spec("3,5,0,7"), "parse roi: rejects zero width");
    check(!parse_roi_spec("-1,5,9,7"), "parse roi: rejects negative origin");
    check(!parse_roi_spec("3,5,9,7x"), "parse roi: rejects trailing junk");
  }

  {
    const auto roi = cfa_balanced_roi(RoiRect{1, 1, 5, 5}, 8, 8);
    check(roi.has_value(), "balanced roi: odd request clips inward");
    if (roi) {
      check(roi->x == 2 && roi->y == 2 && roi->width == 4 &&
                roi->height == 4,
            "balanced roi: even origin and full CFA blocks");
    }
    check(!cfa_balanced_roi(RoiRect{7, 0, 4, 4}, 8, 8),
          "balanced roi: rejects clipped region smaller than one CFA block");
  }

  {
    const auto roi = centered_cfa_balanced_roi(6016, 4014, 0.20);
    check(roi.has_value(), "centered roi: real archive geometry fits");
    if (roi) {
      check(roi->x == 2406 && roi->y == 1606 && roi->width == 1202 &&
                roi->height == 802,
            "centered roi: floors and CFA-balances the published 20% gate");
    }
    check(!centered_cfa_balanced_roi(8, 8, 0.0),
          "centered roi: zero fraction rejects");
    check(!centered_cfa_balanced_roi(8, 8,
                                     std::numeric_limits<double>::infinity()),
          "centered roi: non-finite fraction rejects");
  }

  {
    const auto img = fixture_image();
    const auto report = raw_cfa_report_for_roi(img, RoiRect{2, 0, 4, 4});
    check(report.has_value(), "roi stats: report produced");
    if (report) {
      check(report->measurement_roi.has_value(), "roi stats: actual roi stored");
      check(report->planes[0].label == "R" && report->planes[1].label == "G1" &&
                report->planes[2].label == "G2" &&
                report->planes[3].label == "B",
            "roi stats: labels follow CFA phase");
      check(report->planes[0].count == 4 && report->planes[3].count == 4,
            "roi stats: balanced counts per CFA position");
      check_near(report->planes[0].mean, 25.0, 1e-12,
                 "roi stats: ignores outside pixels");
      check_near(report->planes[1].below_black_fraction, 0.25, 1e-12,
                 "roi stats: below-black fraction from residuals");
      check_near(report->planes[2].stddev, 0.0, 1e-12,
                 "roi stats: flat plane stddev");
      check_near(report->planes[3].saturated_fraction, 0.5, 1e-12,
                 "roi stats: saturation from reconstructed raw value");
    }
  }

  {
    auto img = fixture_image();
    img.samples[2] = std::numeric_limits<double>::quiet_NaN();
    const auto report = raw_cfa_report_for_roi(img, RoiRect{2, 0, 4, 4});
    check(report.has_value(), "roi finite coverage: report produced");
    if (report) {
      check(report->planes[0].count == 3,
            "roi finite coverage: missing sample excluded from denominator");
      check_near(report->planes[0].mean, 30.0, 1e-12,
                 "roi finite coverage: missing sample excluded from aggregate");
    }
  }

  {
    RawCfaImage img;
    img.width = 2;
    img.height = 2;
    img.row_stride_pixels = 2;
    img.color_at_position = {0, 1, 3, 2};
    img.cdesc = "RGBG";
    img.meta.white_level = 16383;
    img.meta.black_per_channel = {1024, 1000, 900, 800};
    // Each residual is the first integer at or above 98% of that plane's
    // signal-referred ceiling, while reconstructed RAW remains below white.
    img.samples = {15052, 15076, 15174, 15272};

    const auto report = raw_cfa_report_for_roi(img, RoiRect{0, 0, 2, 2});
    check(report.has_value(), "roi near-ceiling: report produced");
    if (report) {
      check_near(report->near_ceiling_level,
                 camera_iq::kRawStatsNearCeilingLevel, 1e-12,
                 "roi near-ceiling: effective policy is retained in report");
      for (const auto& plane : report->planes) {
        check_near(plane.near_ceiling_fraction, 1.0, 1e-12,
                   "roi near-ceiling: per-plane signal ceiling is applied");
        check(plane.saturated_fraction == 0,
              "roi near-ceiling: below-white sample is not exact saturation");
      }
    }
  }

  {
    const auto img = fixture_image();
    const auto report = raw_cfa_report_for_roi(img, RoiRect{1, 1, 5, 3});
    check(report.has_value(), "roi stats: odd request can still produce ROI");
    if (report && report->measurement_roi) {
      check(report->measurement_roi->x == 2 && report->measurement_roi->y == 2 &&
                report->measurement_roi->width == 4 &&
                report->measurement_roi->height == 2,
            "roi stats: stored ROI is the CFA-balanced actual region");
      check(report->planes[0].label == "R" && report->planes[1].label == "G1" &&
                report->planes[2].label == "G2" &&
                report->planes[3].label == "B",
            "roi stats: odd-origin snap preserves CFA labels");
      check_near(report->planes[0].mean, 35.0, 1e-12,
                 "roi stats: odd-origin snap keeps R samples on R plane");
      check_near(report->planes[1].mean, 20.0, 1e-12,
                 "roi stats: odd-origin snap keeps G1 samples on G1 plane");
      check_near(report->planes[2].mean, 100.0, 1e-12,
                 "roi stats: odd-origin snap keeps G2 samples on G2 plane");
      check_near(report->planes[3].mean, 448.5, 1e-12,
                 "roi stats: odd-origin snap keeps B samples on B plane");
    }
  }
}
