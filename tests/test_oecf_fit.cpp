#include "camera_iq/oecf_fit.hpp"

#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "camera_iq/exposure_response.hpp"
#include "harness.hpp"

using camera_iq::ChannelStats;
using camera_iq::ExposureResponseSummary;
using camera_iq::ExposureSeries;
using camera_iq::ManifestEntry;
using camera_iq::OecfSeriesFit;
using camera_iq::RawCfaReport;
using camera_iq::fit_oecf_series;
using camera_iq::summarize_exposure_response;
using camera_iq::write_oecf_fit_json;
using test::check;
using test::check_near;

namespace {

ManifestEntry raf(const std::string& name) {
  ManifestEntry e;
  e.relative_path = name;
  e.extension = "raf";
  e.filename_meta = camera_iq::parse_capture_filename(name);
  return e;
}

ExposureSeries series_for(const std::vector<std::string>& paths) {
  ExposureSeries s;
  s.group = "Sphere";
  s.aperture = 8.0;
  s.iso = 200;
  s.paths = paths;
  s.distinct_shutters = paths.size();
  return s;
}

RawCfaReport report(double mean) {
  RawCfaReport out;
  out.meta.make = "Fujifilm";
  out.meta.model = "X-T100";
  out.meta.iso = 200;
  out.meta.aperture = 8;
  out.meta.cfa_pattern = "RGGB";
  out.meta.black_per_channel = {1024, 1024, 1024, 1024};
  out.meta.black_level = 1024;
  out.meta.white_level = 16383;
  out.planes = {
      ChannelStats{"R", 4, mean - 1, mean + 1, mean, 0.5, 0.0, 0.0},
      ChannelStats{"G1", 4, mean - 1, mean + 1, mean, 0.6, 0.0, 0.0},
      ChannelStats{"G2", 4, mean - 1, mean + 1, mean, 0.7, 0.0, 0.0},
      ChannelStats{"B", 4, mean - 1, mean + 1, mean, 0.8, 0.0, 0.0},
  };
  return out;
}

RawCfaReport roi_report(double mean, double spatial_stddev,
                        double saturated_fraction = 0.0) {
  RawCfaReport out = report(mean);
  out.measurement_roi = camera_iq::RoiRect{2, 4, 20, 20};
  for (auto& p : out.planes) {
    p.stddev = spatial_stddev;
    p.saturated_fraction = saturated_fraction;
  }
  return out;
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

void TESTS() {
  {
    const std::vector<ManifestEntry> entries = {
        raf("Sphere_f8.0_1:100_ISO200_DSCF0001.RAF"),
        raf("Sphere_f8.0_1:50_ISO200_DSCF0002.RAF"),
        raf("Sphere_f8.0_1:25_ISO200_DSCF0003.RAF"),
    };
    const auto series = series_for({
        "Sphere_f8.0_1:100_ISO200_DSCF0001.RAF",
        "Sphere_f8.0_1:50_ISO200_DSCF0002.RAF",
        "Sphere_f8.0_1:25_ISO200_DSCF0003.RAF",
    });
    const std::map<std::string, RawCfaReport> reports = {
        {"Sphere_f8.0_1:100_ISO200_DSCF0001.RAF", roi_report(100, 5)},
        {"Sphere_f8.0_1:50_ISO200_DSCF0002.RAF", roi_report(200, 5)},
        {"Sphere_f8.0_1:25_ISO200_DSCF0003.RAF", roi_report(400, 5)},
    };
    const ExposureResponseSummary response =
        summarize_exposure_response(series, entries, reports);

    const OecfSeriesFit fit = fit_oecf_series(response);

    check(fit.fit_candidate, "linear ladder is fit candidate");
    check(fit.plane_fits[0].has_value(), "R fit emitted");
    if (fit.plane_fits[0]) {
      const auto& r = *fit.plane_fits[0];
      check(r.channel == "R", "R fit label");
      check(r.n_points == 3, "linear ladder uses three points");
      check_near(r.slope, 100.0, 1e-12, "linear ladder slope");
      check_near(r.intercept, 0.0, 1e-12, "linear ladder intercept");
      check_near(r.r_squared, 1.0, 1e-12, "linear ladder r2");
      check_near(r.max_nonlinearity_pct, 0.0, 1e-12,
                 "linear ladder max nonlinearity");
      check_near(r.points[0].relative_exposure, 1.0, 1e-12,
                 "relative exposure anchored to fastest usable shutter");
      check(r.points[0].shutter_str == "1:100", "point carries shutter label");
    }
  }

  {
    const std::vector<ManifestEntry> entries = {
        raf("Sphere_f8.0_1:100_ISO200_DSCF0001.RAF"),
        raf("Sphere_f8.0_1:50_ISO200_DSCF0002.RAF"),
        raf("Sphere_f8.0_1:25_ISO200_DSCF0003.RAF"),
    };
    const auto series = series_for({
        "Sphere_f8.0_1:100_ISO200_DSCF0001.RAF",
        "Sphere_f8.0_1:50_ISO200_DSCF0002.RAF",
        "Sphere_f8.0_1:25_ISO200_DSCF0003.RAF",
    });
    const std::map<std::string, RawCfaReport> reports = {
        {"Sphere_f8.0_1:100_ISO200_DSCF0001.RAF", report(-3)},
        {"Sphere_f8.0_1:50_ISO200_DSCF0002.RAF", report(200)},
        {"Sphere_f8.0_1:25_ISO200_DSCF0003.RAF", report(400)},
    };
    const OecfSeriesFit fit = fit_oecf_series(
        summarize_exposure_response(series, entries, reports));

    check(!fit.fit_candidate,
          "fit rejects ladder with fewer than three all-plane-positive points");
    check(!fit.plane_fits[0].has_value(),
          "all-plane-positive gate is inherited by plane fits");
  }

  {
    const std::vector<ManifestEntry> entries = {
        raf("Sphere_f8.0_1:100_ISO200_DSCF0001.RAF"),
        raf("Sphere_f8.0_1:50_ISO200_DSCF0002.RAF"),
        raf("Sphere_f8.0_1:33.3333333333333_ISO200_DSCF0003.RAF"),
        raf("Sphere_f8.0_1:25_ISO200_DSCF0004.RAF"),
    };
    const auto series = series_for({
        "Sphere_f8.0_1:100_ISO200_DSCF0001.RAF",
        "Sphere_f8.0_1:50_ISO200_DSCF0002.RAF",
        "Sphere_f8.0_1:33.3333333333333_ISO200_DSCF0003.RAF",
        "Sphere_f8.0_1:25_ISO200_DSCF0004.RAF",
    });
    const std::map<std::string, RawCfaReport> reports = {
        {"Sphere_f8.0_1:100_ISO200_DSCF0001.RAF", roi_report(10, 5)},
        {"Sphere_f8.0_1:50_ISO200_DSCF0002.RAF", roi_report(20, 5)},
        {"Sphere_f8.0_1:33.3333333333333_ISO200_DSCF0003.RAF",
         roi_report(30, 5)},
        {"Sphere_f8.0_1:25_ISO200_DSCF0004.RAF", roi_report(60, 5)},
    };
    const OecfSeriesFit fit = fit_oecf_series(
        summarize_exposure_response(series, entries, reports));

    check(fit.plane_fits[0].has_value(), "knee ladder emits R fit");
    if (fit.plane_fits[0]) {
      const auto& r = *fit.plane_fits[0];
      check_near(r.slope, 16.0, 1e-9, "knee ladder slope");
      check_near(r.intercept, -10.0, 1e-9, "knee ladder intercept");
      check_near(r.r_squared, 0.9142857142857143, 1e-12, "knee ladder r2");
      check_near(r.max_nonlinearity_pct, 16.666666666666668, 1e-9,
                 "knee ladder max nonlinearity pct");
    }
  }

  {
    const std::vector<ManifestEntry> entries = {
        raf("Sphere_f8.0_1:100_ISO200_DSCF0001.RAF"),
        raf("Sphere_f8.0_1:50_ISO200_DSCF0002.RAF"),
        raf("Sphere_f8.0_1:25_ISO200_DSCF0003.RAF"),
        raf("Sphere_f8.0_1:20_ISO200_DSCF0004.RAF"),
        raf("Sphere_f8.0_1:10_ISO200_DSCF0005.RAF"),
    };
    const auto series = series_for({
        "Sphere_f8.0_1:100_ISO200_DSCF0001.RAF",
        "Sphere_f8.0_1:50_ISO200_DSCF0002.RAF",
        "Sphere_f8.0_1:25_ISO200_DSCF0003.RAF",
        "Sphere_f8.0_1:20_ISO200_DSCF0004.RAF",
        "Sphere_f8.0_1:10_ISO200_DSCF0005.RAF",
    });
    const std::map<std::string, RawCfaReport> reports = {
        {"Sphere_f8.0_1:100_ISO200_DSCF0001.RAF", roi_report(100, 5)},
        {"Sphere_f8.0_1:50_ISO200_DSCF0002.RAF", roi_report(200, 5)},
        {"Sphere_f8.0_1:25_ISO200_DSCF0003.RAF", roi_report(400, 5)},
        // Rejected by inherited gates: below black and non-uniform ROI.
        {"Sphere_f8.0_1:20_ISO200_DSCF0004.RAF", roi_report(-3, 5)},
        {"Sphere_f8.0_1:10_ISO200_DSCF0005.RAF", roi_report(800, 1500)},
    };
    const OecfSeriesFit fit = fit_oecf_series(
        summarize_exposure_response(series, entries, reports));

    check(fit.fit_candidate, "fit remains candidate with three usable points");
    check(fit.usable_oecf_points == 3,
          "below-black and non-uniform ROI points excluded before fit");
    if (fit.plane_fits[0]) {
      check(fit.plane_fits[0]->n_points == 3,
            "only inherited usable points reach R fit");
    }
  }

  {
    const std::vector<ManifestEntry> entries = {
        raf("Sphere_f8.0_1:100_ISO200_DSCF0001.RAF"),
        raf("Sphere_f8.0_1:50_ISO200_DSCF0002.RAF"),
        raf("Sphere_f8.0_1:25_ISO200_DSCF0003.RAF"),
    };
    const auto series = series_for({
        "Sphere_f8.0_1:100_ISO200_DSCF0001.RAF",
        "Sphere_f8.0_1:50_ISO200_DSCF0002.RAF",
        "Sphere_f8.0_1:25_ISO200_DSCF0003.RAF",
    });
    const std::map<std::string, RawCfaReport> reports = {
        {"Sphere_f8.0_1:100_ISO200_DSCF0001.RAF", roi_report(-3, 5)},
        {"Sphere_f8.0_1:50_ISO200_DSCF0002.RAF", roi_report(15357, 5)},
        {"Sphere_f8.0_1:25_ISO200_DSCF0003.RAF", roi_report(800, 1500)},
    };
    const OecfSeriesFit fit = fit_oecf_series(
        summarize_exposure_response(series, entries, reports));

    check(!fit.fit_candidate, "<3 usable points is not fit candidate");
    check(!fit.plane_fits[0].has_value(), "<3 usable points emits null fit");
    check(!fit.limitations.empty(), "<3 usable points records limitation");
  }

  {
    const std::vector<ManifestEntry> entries = {
        raf("Sphere_f8.0_1:100_ISO200_DSCF0001.RAF"),
        raf("Sphere_f8.0_1:50_ISO200_DSCF0002.RAF"),
        raf("Sphere_f8.0_1:25_ISO200_DSCF0003.RAF"),
    };
    const auto series = series_for({
        "Sphere_f8.0_1:100_ISO200_DSCF0001.RAF",
        "Sphere_f8.0_1:50_ISO200_DSCF0002.RAF",
        "Sphere_f8.0_1:25_ISO200_DSCF0003.RAF",
    });
    const std::map<std::string, RawCfaReport> reports = {
        {"Sphere_f8.0_1:100_ISO200_DSCF0001.RAF", roi_report(100, 5)},
        {"Sphere_f8.0_1:50_ISO200_DSCF0002.RAF", roi_report(200, 5)},
        {"Sphere_f8.0_1:25_ISO200_DSCF0003.RAF", roi_report(400, 5)},
    };
    const OecfSeriesFit fit = fit_oecf_series(
        summarize_exposure_response(series, entries, reports));
    std::ostringstream json;
    write_oecf_fit_json(json, "dataset:fixture", {fit});
    const std::string doc = json.str();
    check(contains(doc, "\"mode\":\"oecf-fit\""), "json mode");
    check(contains(doc, "\"root\":\"dataset:fixture\""), "json root");
    check(contains(doc, "\"fit_candidate\":true"), "json fit candidate");
    check(contains(doc, "\"max_nonlinearity_pct\""),
          "json headline linearity metric");
    check(contains(doc, "\"r_squared\""), "json r squared metric");
    check(contains(doc, "not ISO 14524"), "json non-ISO limitation");
  }
}
