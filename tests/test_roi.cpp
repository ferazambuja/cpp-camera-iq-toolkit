#include "camera_iq/roi.hpp"
#include "camera_iq/raw_meta.hpp"

#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::RawCfaImage;
using camera_iq::RoiRect;
using camera_iq::cfa_balanced_roi;
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
    const auto img = fixture_image();
    const auto report = raw_cfa_report_for_roi(img, RoiRect{1, 1, 5, 3});
    check(report.has_value(), "roi stats: odd request can still produce ROI");
    if (report && report->measurement_roi) {
      check(report->measurement_roi->x == 2 && report->measurement_roi->y == 2 &&
                report->measurement_roi->width == 4 &&
                report->measurement_roi->height == 2,
            "roi stats: stored ROI is the CFA-balanced actual region");
    }
  }
}
