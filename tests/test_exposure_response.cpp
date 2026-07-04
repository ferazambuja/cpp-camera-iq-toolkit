#include "camera_iq/exposure_response.hpp"

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::ChannelStats;
using camera_iq::ExposureSeries;
using camera_iq::ManifestEntry;
using camera_iq::RawCfaReport;
using camera_iq::summarize_exposure_response;
using camera_iq::write_exposure_response_json;
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

RawCfaReport report(double r, double g1, double b, double g2) {
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
      ChannelStats{"R", 4, r - 1, r + 1, r, 0.5, 0.0, 0.0},
      ChannelStats{"G1", 4, g1 - 1, g1 + 1, g1, 0.6, 0.0, 0.0},
      ChannelStats{"B", 4, b - 1, b + 1, b, 0.7, 0.0, 0.0},
      ChannelStats{"G2", 4, g2 - 1, g2 + 1, g2, 0.8, 0.0, 0.0},
  };
  return out;
}

// Same as report() but with a per-plane saturated fraction, for testing the
// clipping veto independently of the mean-of-range proxy.
RawCfaReport report_sat(double mean, double saturated_fraction) {
  RawCfaReport out = report(mean, mean, mean, mean);
  for (auto& p : out.planes) p.saturated_fraction = saturated_fraction;
  return out;
}

RawCfaReport roi_report(double mean, double spatial_stddev) {
  RawCfaReport out = report(mean, mean, mean, mean);
  out.measurement_roi = camera_iq::RoiRect{2, 4, 20, 22};
  for (auto& p : out.planes) p.stddev = spatial_stddev;
  return out;
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

void TESTS() {
  std::vector<ManifestEntry> entries = {
      raf("Sphere_f8.0_1:100_DSCF0001.RAF"),
      raf("Sphere_f8.0_1:50_DSCF0002.RAF"),
      raf("Sphere_f8.0_1:25_DSCF0003.RAF"),
      // Duplicate shutter: should aggregate into the same exposure point.
      raf("Sphere_f8.0_1:25_DSCF0004.RAF"),
  };

  ExposureSeries series;
  series.group = "Sphere";
  series.aperture = 8.0;
  series.paths = {
      "Sphere_f8.0_1:100_DSCF0001.RAF",
      "Sphere_f8.0_1:50_DSCF0002.RAF",
      "Sphere_f8.0_1:25_DSCF0003.RAF",
      "Sphere_f8.0_1:25_DSCF0004.RAF",
  };
  series.distinct_shutters = 3;

  const std::map<std::string, RawCfaReport> reports = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", report(100, 101, 102, 103)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", report(200, 201, 202, 203)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", report(400, 401, 402, 403)},
      {"Sphere_f8.0_1:25_DSCF0004.RAF", report(420, 421, 422, 423)},
  };

  const auto summary = summarize_exposure_response(series, entries, reports);

  check(summary.readable_frames == 4, "all frames readable");
  check(summary.missing_reports == 0, "no missing reports");
  check(summary.exif_consistent, "exif controls consistent");
  check(summary.points.size() == 3, "three shutter points");
  check(summary.oecf_candidate, "three-point fixed series is OECF candidate");
  check(!summary.ptc_candidate, "PTC is not claimed from summary stats");
  check(!summary.limitations.empty(), "PTC limitation is explicit");

  if (summary.points.size() == 3) {
    check_near(summary.points[0].shutter_s, 0.01, 1e-12, "points sorted fast to slow");
    check(summary.points[0].shutter_str == "1:100", "first shutter label");
    check_near(summary.points[1].mean_signal_by_plane[0], 200.0, 1e-12,
               "single-frame mean preserved");
    check(summary.points[2].frames.size() == 2, "duplicate shutter grouped");
    check_near(summary.points[2].mean_signal_by_plane[0], 410.0, 1e-12,
               "duplicate shutter mean averaged");
    check_near(summary.points[2].mean_spatial_stddev_by_plane[3], 0.8, 1e-12,
               "per-plane spatial stddev averaged, not called temporal noise");
    check(summary.points[0].usable_oecf, "point records usable OECF gate");
  }

  const std::map<std::string, RawCfaReport> missing_one = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", report(100, 101, 102, 103)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", report(200, 201, 202, 203)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", report(400, 401, 402, 403)},
  };
  const auto incomplete =
      summarize_exposure_response(series, entries, missing_one);
  check(incomplete.readable_frames == 3, "readable count excludes failures");
  check(incomplete.missing_reports == 1, "missing report counted");
  check(!incomplete.oecf_candidate, "incomplete series is not candidate-ready");

  const std::map<std::string, RawCfaReport> clipped_reports = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", report(15357, 15357, 15357, 15357)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", report(15357, 15357, 15357, 15357)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", report(15357, 15357, 15357, 15357)},
      {"Sphere_f8.0_1:25_DSCF0004.RAF", report(15357, 15357, 15357, 15357)},
  };
  const auto clipped =
      summarize_exposure_response(series, entries, clipped_reports);
  check(clipped.usable_oecf_points == 0,
        "near-white plateau has zero usable OECF points");
  check(!clipped.oecf_candidate,
        "near-white plateau is not candidate-ready");

  auto changed_iso = reports;
  changed_iso["Sphere_f8.0_1:50_DSCF0002.RAF"].meta.iso = 400;
  const auto inconsistent =
      summarize_exposure_response(series, entries, changed_iso);
  check(!inconsistent.exif_consistent, "changed ISO breaks EXIF consistency");
  check(!inconsistent.oecf_candidate,
        "EXIF-inconsistent series is not candidate-ready");

  // Lower-bound guard: frames at/below black are nowhere near white, but they
  // carry no OECF signal and must not be promoted to usable points.
  const std::map<std::string, RawCfaReport> dark_reports = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", report(-3, -3, -3, -3)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", report(-3, -3, -3, -3)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", report(-3, -3, -3, -3)},
      {"Sphere_f8.0_1:25_DSCF0004.RAF", report(-3, -3, -3, -3)},
  };
  const auto dark = summarize_exposure_response(series, entries, dark_reports);
  check(dark.usable_oecf_points == 0,
        "below-black frames are not usable OECF points");
  check(!dark.oecf_candidate, "below-black ladder is not candidate-ready");

  // Per-plane lower-bound guard: a point where only some planes are above black
  // is not usable for a per-CFA-plane OECF fit.
  const std::map<std::string, RawCfaReport> mixed_dark_reports = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", report(-3, 100, 100, 100)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", report(-3, 200, 200, 200)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", report(-3, 400, 400, 400)},
      {"Sphere_f8.0_1:25_DSCF0004.RAF", report(-3, 420, 420, 420)},
  };
  const auto mixed_dark =
      summarize_exposure_response(series, entries, mixed_dark_reports);
  check(mixed_dark.usable_oecf_points == 0,
        "any below-black CFA plane rejects an OECF point");
  check(!mixed_dark.oecf_candidate,
        "mixed below-black ladder is not candidate-ready");

  // Clipping veto: mid-range mean would pass the near-white proxy, but heavy
  // measured saturation must reject the point (non-uniform highlight clipping).
  const std::map<std::string, RawCfaReport> clipped_by_fraction = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", report_sat(8000, 0.20)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", report_sat(8000, 0.20)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", report_sat(8000, 0.20)},
      {"Sphere_f8.0_1:25_DSCF0004.RAF", report_sat(8000, 0.20)},
  };
  const auto sat =
      summarize_exposure_response(series, entries, clipped_by_fraction);
  check(sat.usable_oecf_points == 0,
        "heavy saturated fraction rejects points despite mid-range mean");
  check(!sat.oecf_candidate, "clipped-by-fraction ladder is not candidate-ready");

  std::ostringstream json;
  write_exposure_response_json(json, "fixture-root",
                               {summary, incomplete, clipped, inconsistent});
  const std::string doc = json.str();
  check(contains(doc, "\"mode\":\"exposure-response\""), "json mode");
  check(contains(doc, "\"root\":\"fixture-root\""), "json root");
  check(contains(doc, "\"oecf_candidate\":true"), "json oecf candidate");
  check(contains(doc, "\"exif_consistent\":false"), "json exif gate");
  check(contains(doc, "\"ptc_candidate\":false"), "json ptc limitation flag");
  check(contains(doc, "\"mean_signal_by_plane\""), "json signal means");
  check(contains(doc, "\"mean_spatial_stddev_by_plane\""),
        "json spatial stddev named honestly");
  check(contains(doc, "\"usable_oecf_points\":3"), "json usable point count");
  check(contains(doc, "\"max_mean_fraction_of_range\""),
        "json range headroom metric");
  check(contains(doc, "\"min_mean_fraction_of_range\""),
        "json lower-bound signal metric");
  check(contains(doc, "\"usable_oecf\":true"),
        "json records per-point usable OECF gate");
  check(contains(doc, "\"missing_reports\":1"), "json missing count");
  check(contains(doc, "\"roi_uniformity_checked\":false"),
        "json records full-frame ROI uniformity not checked");
  check(contains(doc, "\"roi_uniform\":null"),
        "json does not imply full-frame ROI uniformity passed");

  auto roi_reports = reports;
  roi_reports["Sphere_f8.0_1:100_DSCF0001.RAF"].measurement_roi =
      camera_iq::RoiRect{2, 4, 20, 22};
  const auto roi_summary =
      summarize_exposure_response(series, entries, roi_reports);
  std::ostringstream roi_json;
  write_exposure_response_json(roi_json, "fixture-root", {roi_summary});
  const std::string roi_doc = roi_json.str();
  check(contains(roi_doc, "\"measurement_roi\""),
        "json records ROI measurement region");
  check(contains(roi_doc, "\"x\":2") && contains(roi_doc, "\"height\":22"),
        "json records ROI coordinates");

  const std::map<std::string, RawCfaReport> nonuniform_roi_reports = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", roi_report(8000, 1500)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", roi_report(8000, 1500)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", roi_report(8000, 1500)},
      {"Sphere_f8.0_1:25_DSCF0004.RAF", roi_report(8000, 1500)},
  };
  const auto nonuniform_roi =
      summarize_exposure_response(series, entries, nonuniform_roi_reports);
  check(nonuniform_roi.usable_oecf_points == 0,
        "non-uniform ROI points are not usable OECF points");
  check(!nonuniform_roi.oecf_candidate,
        "non-uniform ROI ladder is not candidate-ready");

  std::ostringstream nonuniform_json;
  write_exposure_response_json(nonuniform_json, "fixture-root",
                               {nonuniform_roi});
  const std::string nonuniform_doc = nonuniform_json.str();
  check(contains(nonuniform_doc, "\"roi_uniformity_checked\":true"),
        "json records ROI uniformity gate was checked");
  check(contains(nonuniform_doc, "\"roi_uniform\":false"),
        "json records ROI uniformity failure");
  check(contains(nonuniform_doc,
                 "\"max_spatial_stddev_fraction_of_range\""),
        "json records ROI spatial stddev fraction");

  // Accept-path complement to the non-uniform reject above. Without this, a
  // regression that over-rejects uniform ROIs in ROI mode (tighter threshold or
  // wrong comparison direction) would pass every other test: the full-frame
  // candidate test is not ROI mode, and the non-uniform test expects zero usable
  // either way. A spatially uniform ROI ladder must still be promoted.
  const std::map<std::string, RawCfaReport> uniform_roi_reports = {
      {"Sphere_f8.0_1:100_DSCF0001.RAF", roi_report(100, 50)},
      {"Sphere_f8.0_1:50_DSCF0002.RAF", roi_report(200, 50)},
      {"Sphere_f8.0_1:25_DSCF0003.RAF", roi_report(400, 50)},
      {"Sphere_f8.0_1:25_DSCF0004.RAF", roi_report(420, 50)},
  };
  const auto uniform_roi =
      summarize_exposure_response(series, entries, uniform_roi_reports);
  check(uniform_roi.usable_oecf_points == 3,
        "uniform ROI ladder: all three shutter points usable");
  check(uniform_roi.oecf_candidate,
        "uniform ROI ladder is candidate-ready");

  std::ostringstream uniform_json;
  write_exposure_response_json(uniform_json, "fixture-root", {uniform_roi});
  check(contains(uniform_json.str(), "\"roi_uniform\":true"),
        "json records uniform ROI passed the gate");
}
