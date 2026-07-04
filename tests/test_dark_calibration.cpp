#include "camera_iq/dark_calibration.hpp"

#include <array>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::ChannelStats;
using camera_iq::DarkCalibrationSummary;
using camera_iq::ManifestEntry;
using camera_iq::RawCfaReport;
using camera_iq::summarize_dark_calibration;
using camera_iq::write_dark_calibration_json;
using test::check;
using test::check_near;

namespace {

ManifestEntry dark_raf(const std::string& name) {
  ManifestEntry e;
  e.relative_path = name;
  e.extension = "raf";
  e.filename_meta = camera_iq::parse_capture_filename(name);
  return e;
}

RawCfaReport dark_report(const std::array<double, 4>& residuals,
                         const std::array<double, 4>& stddevs = {3, 4, 5, 6}) {
  RawCfaReport out;
  out.meta.make = "Fujifilm";
  out.meta.model = "X-T100";
  out.meta.cfa_pattern = "RGGB";
  out.meta.black_per_channel = {1024, 1024, 1024, 1024};
  out.meta.black_level = 1024;
  out.meta.white_level = 16383;
  out.planes = {
      ChannelStats{"R", 4, residuals[0] - 1, residuals[0] + 1,
                   residuals[0], stddevs[0], 0.5, 0.0},
      ChannelStats{"G1", 4, residuals[1] - 1, residuals[1] + 1,
                   residuals[1], stddevs[1], 0.5, 0.0},
      ChannelStats{"G2", 4, residuals[2] - 1, residuals[2] + 1,
                   residuals[2], stddevs[2], 0.5, 0.0},
      ChannelStats{"B", 4, residuals[3] - 1, residuals[3] + 1,
                   residuals[3], stddevs[3], 0.5, 0.0},
  };
  return out;
}

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

}  // namespace

void TESTS() {
  const std::vector<ManifestEntry> entries = {
      dark_raf("Dark_Frame_f8.0_1:60_ISO200_DSCF0269.RAF"),
      dark_raf("Dark_Frame_f8.0_1:125_ISO200_DSCF0272.RAF"),
      dark_raf("Dark_Frame_f8.0_1:250_ISO200_DSCF0273.RAF"),
  };

  const std::map<std::string, RawCfaReport> reports = {
      {"Dark_Frame_f8.0_1:60_ISO200_DSCF0269.RAF",
       dark_report({-0.25, 0.0, 0.25, 0.5})},
      {"Dark_Frame_f8.0_1:125_ISO200_DSCF0272.RAF",
       dark_report({0.25, 0.5, -0.25, 0.0})},
  };

  const DarkCalibrationSummary summary =
      summarize_dark_calibration(entries, reports, 2.0);
  check(summary.candidate_frames == 3, "candidate count includes all entries");
  check(summary.readable_frames == 2, "readable reports counted");
  check(summary.frames_within_tolerance == 2,
        "small-residual frames are counted within tolerance");
  check(summary.outlier_frames == 0, "small-residual frames are not outliers");
  check(summary.missing_reports == 1, "missing report counted");
  check(!summary.metadata_black_consistent_with_dark,
        "missing reports prevent set-level consistency");
  check_near(summary.mean_residual_by_plane[0], 0.0, 1e-12,
             "mean R residual averages signed values");
  check_near(summary.mean_residual_by_plane[1], 0.25, 1e-12,
             "mean G1 residual");
  check_near(summary.mean_measured_dark_raw_by_plane[1], 1024.25, 1e-12,
             "measured raw dark = metadata black + residual");
  check_near(summary.mean_in_tolerance_residual_by_plane[1], 0.25, 1e-12,
             "in-tolerance residual mean excludes only outliers");
  check_near(summary.mean_in_tolerance_measured_dark_raw_by_plane[1], 1024.25,
             1e-12, "in-tolerance raw dark mean");
  check_near(summary.mean_spatial_stddev_by_plane[3], 6.0, 1e-12,
             "spatial stddev averaged separately");
  check_near(summary.max_abs_mean_residual, 0.5, 1e-12,
             "max per-frame absolute mean residual retained");
  check(!summary.limitations.empty(), "missing report limitation is explicit");

  const std::map<std::string, RawCfaReport> biased_reports = {
      {"Dark_Frame_f8.0_1:60_ISO200_DSCF0269.RAF",
       dark_report({12, 10, 11, 9})},
      {"Dark_Frame_f8.0_1:125_ISO200_DSCF0272.RAF",
       dark_report({13, 10, 11, 9})},
      {"Dark_Frame_f8.0_1:250_ISO200_DSCF0273.RAF",
       dark_report({14, 10, 11, 9})},
  };
  const DarkCalibrationSummary biased =
      summarize_dark_calibration(entries, biased_reports, 2.0);
  check(!biased.metadata_black_consistent_with_dark,
        "large dark residuals fail metadata-black reconciliation");
  check(biased.frames_within_tolerance == 0,
        "large-residual frames are excluded from tolerance count");
  check(biased.outlier_frames == 3, "large-residual frames are counted outliers");
  check_near(biased.mean_in_tolerance_residual_by_plane[0], 0.0, 1e-12,
             "no in-tolerance frames leaves inlier residual means at zero");
  check(biased.limitations.size() == 1,
        "large residual limitation is explicit");

  const std::vector<ManifestEntry> complete_entries = {
      dark_raf("Dark_Frame_f8.0_1:60_ISO200_DSCF0269.RAF"),
      dark_raf("Dark_Frame_f8.0_1:125_ISO200_DSCF0272.RAF"),
  };
  const DarkCalibrationSummary complete =
      summarize_dark_calibration(complete_entries, reports, 2.0);
  check(complete.metadata_black_consistent_with_dark,
        "complete small-residual set is consistent with metadata black");

  std::ostringstream json;
  write_dark_calibration_json(json, "dataset:fixture/Images/Dark Frame",
                              complete);
  const std::string doc = json.str();
  check(contains(doc, "\"mode\":\"dark-calibration\""), "json mode");
  check(contains(doc, "\"root\":\"dataset:fixture/Images/Dark Frame\""),
        "json uses dataset label");
  check(contains(doc, "\"metadata_black_consistent_with_dark\":true"),
        "json consistency flag");
  check(contains(doc, "\"frames_within_tolerance\":2"),
        "json tolerance count");
  check(contains(doc, "\"mean_measured_dark_raw_by_plane\""),
        "json measured raw dark means");
  check(contains(doc, "\"mean_in_tolerance_measured_dark_raw_by_plane\""),
        "json in-tolerance raw dark means");
  const std::string home_prefix = "/" "Users/";
  const std::string volume_prefix = "/" "Volumes/";
  check(!contains(doc, home_prefix) && !contains(doc, volume_prefix),
        "json fixture contains no private absolute paths");
}
