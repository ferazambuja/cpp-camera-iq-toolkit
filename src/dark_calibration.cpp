#include "camera_iq/dark_calibration.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <utility>

#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

bool is_raw_extension(std::string_view ext) {
  return ext == "raf" || ext == "nef" || ext == "arw" || ext == "cr2" ||
         ext == "iiq" || ext == "dng";
}

void add_arrays(std::array<double, 4>& dst,
                const std::array<double, 4>& src) {
  for (std::size_t i = 0; i < dst.size(); ++i) dst[i] += src[i];
}

void divide_array(std::array<double, 4>& values, double n) {
  if (n == 0.0) return;
  for (double& v : values) v /= n;
}

void write_double_array(JsonWriter& w, const std::array<double, 4>& values) {
  w.begin_array();
  for (double v : values) w.value(v);
  w.end_array();
}

}  // namespace

DarkCalibrationSummary summarize_dark_calibration(
    const std::vector<ManifestEntry>& entries,
    const std::map<std::string, RawCfaReport>& reports_by_path,
    double residual_tolerance_dn) {
  DarkCalibrationSummary summary;
  summary.residual_tolerance_dn = residual_tolerance_dn;
  if (summary.residual_tolerance_dn < 0.0) {
    summary.residual_tolerance_dn = 0.0;
  }

  for (const auto& entry : entries) {
    if (!is_raw_extension(entry.extension)) continue;
    ++summary.candidate_frames;

    const auto report_it = reports_by_path.find(entry.relative_path);
    if (report_it == reports_by_path.end()) {
      ++summary.missing_reports;
      continue;
    }

    const RawCfaReport& report = report_it->second;
    DarkCalibrationFrame frame;
    frame.path = entry.relative_path;
    frame.shutter_str = entry.filename_meta.shutter_str.value_or("");
    frame.shutter_s = entry.filename_meta.shutter_s.value_or(0.0);
    frame.metadata_black_by_plane = report.meta.black_per_channel;
    for (std::size_t i = 0; i < frame.mean_residual_by_plane.size(); ++i) {
      frame.mean_residual_by_plane[i] = report.planes[i].mean;
      frame.measured_dark_raw_by_plane[i] =
          report.meta.black_per_channel[i] + report.planes[i].mean;
      frame.spatial_stddev_by_plane[i] = report.planes[i].stddev;
      frame.max_abs_mean_residual =
          std::max(frame.max_abs_mean_residual, std::abs(report.planes[i].mean));
    }
    frame.within_residual_tolerance =
        frame.max_abs_mean_residual <= summary.residual_tolerance_dn;
    if (frame.within_residual_tolerance) {
      ++summary.frames_within_tolerance;
      add_arrays(summary.mean_in_tolerance_residual_by_plane,
                 frame.mean_residual_by_plane);
      add_arrays(summary.mean_in_tolerance_measured_dark_raw_by_plane,
                 frame.measured_dark_raw_by_plane);
    } else {
      ++summary.outlier_frames;
    }

    add_arrays(summary.mean_metadata_black_by_plane,
               frame.metadata_black_by_plane);
    add_arrays(summary.mean_residual_by_plane, frame.mean_residual_by_plane);
    add_arrays(summary.mean_measured_dark_raw_by_plane,
               frame.measured_dark_raw_by_plane);
    add_arrays(summary.mean_spatial_stddev_by_plane,
               frame.spatial_stddev_by_plane);
    summary.max_abs_mean_residual =
        std::max(summary.max_abs_mean_residual, frame.max_abs_mean_residual);
    summary.frames.push_back(std::move(frame));
    ++summary.readable_frames;
  }

  const double readable = static_cast<double>(summary.readable_frames);
  divide_array(summary.mean_metadata_black_by_plane, readable);
  divide_array(summary.mean_residual_by_plane, readable);
  divide_array(summary.mean_measured_dark_raw_by_plane, readable);
  divide_array(summary.mean_spatial_stddev_by_plane, readable);
  const double in_tolerance =
      static_cast<double>(summary.frames_within_tolerance);
  divide_array(summary.mean_in_tolerance_residual_by_plane, in_tolerance);
  divide_array(summary.mean_in_tolerance_measured_dark_raw_by_plane,
               in_tolerance);

  summary.metadata_black_consistent_with_dark =
      summary.readable_frames > 0 && summary.missing_reports == 0 &&
      summary.outlier_frames == 0;

  if (summary.missing_reports != 0) {
    summary.limitations.push_back(
        "One or more dark-frame files could not be read or unpacked.");
  }
  if (summary.readable_frames == 0) {
    summary.limitations.push_back(
        "No readable dark frames; metadata black cannot be reconciled.");
  } else if (summary.outlier_frames != 0) {
    summary.limitations.push_back(
        "One or more dark-frame residual means exceed the configured tolerance; inspect "
        "metadata black, dark current, and capture conditions before using "
        "these frames for noise or dynamic-range metrics.");
  }
  return summary;
}

void write_dark_calibration_json(std::ostream& os, std::string_view root_label,
                                 const DarkCalibrationSummary& summary) {
  JsonWriter w(os);
  w.begin_object();
  w.key("tool");
  w.value("camera_iq");
  w.key("mode");
  w.value("dark-calibration");
  w.key("root");
  w.value(root_label);
  w.key("candidate_frames");
  w.value(static_cast<std::int64_t>(summary.candidate_frames));
  w.key("readable_frames");
  w.value(static_cast<std::int64_t>(summary.readable_frames));
  w.key("frames_within_tolerance");
  w.value(static_cast<std::int64_t>(summary.frames_within_tolerance));
  w.key("outlier_frames");
  w.value(static_cast<std::int64_t>(summary.outlier_frames));
  w.key("missing_reports");
  w.value(static_cast<std::int64_t>(summary.missing_reports));
  w.key("residual_tolerance_dn");
  w.value(summary.residual_tolerance_dn);
  w.key("metadata_black_consistent_with_dark");
  w.value(summary.metadata_black_consistent_with_dark);
  w.key("max_abs_mean_residual");
  w.value(summary.max_abs_mean_residual);
  w.key("mean_metadata_black_by_plane");
  write_double_array(w, summary.mean_metadata_black_by_plane);
  w.key("mean_residual_by_plane");
  write_double_array(w, summary.mean_residual_by_plane);
  w.key("mean_measured_dark_raw_by_plane");
  write_double_array(w, summary.mean_measured_dark_raw_by_plane);
  w.key("mean_spatial_stddev_by_plane");
  write_double_array(w, summary.mean_spatial_stddev_by_plane);
  w.key("mean_in_tolerance_residual_by_plane");
  write_double_array(w, summary.mean_in_tolerance_residual_by_plane);
  w.key("mean_in_tolerance_measured_dark_raw_by_plane");
  write_double_array(w, summary.mean_in_tolerance_measured_dark_raw_by_plane);

  w.key("limitations");
  w.begin_array();
  for (const auto& limitation : summary.limitations) w.value(limitation);
  w.end_array();

  w.key("frames");
  w.begin_array();
  for (const auto& frame : summary.frames) {
    w.begin_object();
    w.key("path");
    w.value(frame.path);
    w.key("shutter_s");
    w.value(frame.shutter_s);
    w.key("shutter_str");
    w.value(frame.shutter_str);
    w.key("max_abs_mean_residual");
    w.value(frame.max_abs_mean_residual);
    w.key("within_residual_tolerance");
    w.value(frame.within_residual_tolerance);
    w.key("metadata_black_by_plane");
    write_double_array(w, frame.metadata_black_by_plane);
    w.key("mean_residual_by_plane");
    write_double_array(w, frame.mean_residual_by_plane);
    w.key("measured_dark_raw_by_plane");
    write_double_array(w, frame.measured_dark_raw_by_plane);
    w.key("spatial_stddev_by_plane");
    write_double_array(w, frame.spatial_stddev_by_plane);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

}  // namespace camera_iq
