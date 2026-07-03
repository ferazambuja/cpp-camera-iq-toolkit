#include "camera_iq/exposure_response.hpp"

#include "camera_iq/json_writer.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>

namespace camera_iq {
namespace {

constexpr double kOecfNearWhiteMeanFraction = 0.98;
// A usable OECF point must also not clip: reject a point where more than this
// fraction of pixels sits at/above white, even if the mean still reads
// mid-range. A non-uniform frame can clip its highlights while the spatial mean
// looks unsaturated, so the measured saturated fraction is a stricter guard
// than the mean-of-range proxy alone.
constexpr double kOecfMaxSaturatedFraction = 0.01;

const ManifestEntry* find_entry(const std::vector<ManifestEntry>& entries,
                                const std::string& path) {
  for (const auto& e : entries) {
    if (e.relative_path == path) return &e;
  }
  return nullptr;
}

bool near_equal(double a, double b) {
  return std::abs(a - b) <= 1e-6;
}

bool same_exif_controls(const RawMeta& a, const RawMeta& b) {
  return a.make == b.make && a.model == b.model &&
         a.cfa_pattern == b.cfa_pattern && near_equal(a.iso, b.iso) &&
         near_equal(a.aperture, b.aperture);
}

void recompute_point_averages(ExposureResponsePoint& point) {
  point.mean_signal_by_plane = {0, 0, 0, 0};
  point.mean_spatial_stddev_by_plane = {0, 0, 0, 0};
  point.has_valid_signal_range = false;
  point.max_mean_fraction_of_range = 0;
  point.max_saturated_fraction = 0;
  if (point.frames.empty()) return;

  for (const auto& frame : point.frames) {
    for (std::size_t i = 0; i < point.mean_signal_by_plane.size(); ++i) {
      point.mean_signal_by_plane[i] += frame.planes[i].mean;
      point.mean_spatial_stddev_by_plane[i] += frame.planes[i].stddev;
      point.max_saturated_fraction = std::max(
          point.max_saturated_fraction, frame.planes[i].saturated_fraction);
      const double range = frame.white_level - frame.black_per_channel[i];
      if (range > 0) {
        point.has_valid_signal_range = true;
        point.max_mean_fraction_of_range = std::max(
            point.max_mean_fraction_of_range, frame.planes[i].mean / range);
      }
    }
  }
  const double n = static_cast<double>(point.frames.size());
  for (std::size_t i = 0; i < point.mean_signal_by_plane.size(); ++i) {
    point.mean_signal_by_plane[i] /= n;
    point.mean_spatial_stddev_by_plane[i] /= n;
  }
}

void write_optional(JsonWriter& w, const std::optional<double>& v) {
  if (v) {
    w.value(*v);
  } else {
    w.null();
  }
}

void write_optional(JsonWriter& w, const std::optional<int>& v) {
  if (v) {
    w.value(*v);
  } else {
    w.null();
  }
}

void write_plane_stats(JsonWriter& w, const ChannelStats& p) {
  w.begin_object();
  w.key("channel");
  w.value(p.label);
  w.key("count");
  w.value(static_cast<std::int64_t>(p.count));
  w.key("min");
  w.value(p.min);
  w.key("max");
  w.value(p.max);
  w.key("mean");
  w.value(p.mean);
  w.key("stddev");
  w.value(p.stddev);
  w.key("below_black_fraction");
  w.value(p.below_black_fraction);
  w.key("saturated_fraction");
  w.value(p.saturated_fraction);
  w.end_object();
}

void write_double_array(JsonWriter& w, const std::array<double, 4>& values) {
  w.begin_array();
  for (double v : values) w.value(v);
  w.end_array();
}

}  // namespace

ExposureResponseSummary summarize_exposure_response(
    const ExposureSeries& series, const std::vector<ManifestEntry>& entries,
    const std::map<std::string, RawCfaReport>& reports_by_path) {
  ExposureResponseSummary summary;
  summary.series = series;

  std::optional<RawMeta> first_meta;
  std::map<double, ExposureResponsePoint> points_by_shutter;
  for (const auto& path : series.paths) {
    const ManifestEntry* entry = find_entry(entries, path);
    if (entry == nullptr || !entry->filename_meta.shutter_s) {
      ++summary.missing_reports;
      continue;
    }

    const auto report_it = reports_by_path.find(path);
    if (report_it == reports_by_path.end()) {
      ++summary.missing_reports;
      continue;
    }
    if (!first_meta) {
      first_meta = report_it->second.meta;
    } else if (!same_exif_controls(*first_meta, report_it->second.meta)) {
      summary.exif_consistent = false;
    }

    const double shutter_s = *entry->filename_meta.shutter_s;
    auto& point = points_by_shutter[shutter_s];
    if (point.frames.empty()) {
      point.shutter_s = shutter_s;
      point.shutter_str = entry->filename_meta.shutter_str.value_or("");
    }

    ExposureResponseFrame frame;
    frame.path = path;
    frame.shutter_s = shutter_s;
    frame.shutter_str = entry->filename_meta.shutter_str.value_or("");
    frame.planes = report_it->second.planes;
    frame.black_per_channel = report_it->second.meta.black_per_channel;
    frame.white_level = report_it->second.meta.white_level;
    point.frames.push_back(std::move(frame));
    ++summary.readable_frames;
  }

  summary.points.reserve(points_by_shutter.size());
  for (auto& [shutter, point] : points_by_shutter) {
    (void)shutter;
    recompute_point_averages(point);
    // Usable = real signal above black (fraction > 0), below the near-white
    // plateau, and below the per-pixel clipping tolerance. The lower bound is
    // what makes the flag conservative at the dark end: a point at or below
    // black carries no OECF information and must not be counted.
    if (point.has_valid_signal_range &&
        point.max_mean_fraction_of_range > 0.0 &&
        point.max_mean_fraction_of_range < kOecfNearWhiteMeanFraction &&
        point.max_saturated_fraction < kOecfMaxSaturatedFraction) {
      ++summary.usable_oecf_points;
    }
    summary.points.push_back(std::move(point));
  }

  summary.oecf_candidate =
      summary.missing_reports == 0 && summary.readable_frames >= 3 &&
      summary.points.size() >= 3 && summary.usable_oecf_points >= 3 &&
      summary.exif_consistent;

  summary.ptc_candidate = false;
  summary.limitations.push_back(
      "PTC not computed: this summary uses per-frame full-frame spatial "
      "stddev; photon-transfer/read-noise validation needs repeated-frame "
      "temporal or per-pixel variance plus ROI/flat-field controls.");
  if (summary.missing_reports != 0) {
    summary.limitations.push_back(
        "OECF candidate flag is false because one or more series files could "
        "not be read or lacked filename shutter metadata.");
  }
  if (summary.points.size() >= 3 && summary.usable_oecf_points < 3) {
    summary.limitations.push_back(
        "OECF candidate flag is false because fewer than three shutter points "
        "carry usable signal: each usable point must be above black, below 98% "
        "of the black-subtracted white range, and below 1% saturated pixels.");
  }
  if (!summary.exif_consistent) {
    summary.limitations.push_back(
        "OECF candidate flag is false because readable frames do not share "
        "the same EXIF make/model/CFA/ISO/aperture controls.");
  }

  return summary;
}

void write_exposure_response_json(
    std::ostream& os, std::string_view root_label,
    const std::vector<ExposureResponseSummary>& summaries) {
  JsonWriter w(os);
  w.begin_object();
  w.key("tool");
  w.value("camera_iq");
  w.key("mode");
  w.value("exposure-response");
  w.key("root");
  w.value(root_label);
  w.key("series_count");
  w.value(static_cast<std::int64_t>(summaries.size()));

  w.key("series");
  w.begin_array();
  for (const auto& s : summaries) {
    w.begin_object();
    w.key("directory");
    w.value(s.series.directory);
    w.key("group");
    w.value(s.series.group);
    w.key("aperture");
    write_optional(w, s.series.aperture);
    w.key("iso");
    write_optional(w, s.series.iso);
    w.key("distinct_shutters");
    w.value(static_cast<std::int64_t>(s.series.distinct_shutters));
    w.key("frame_count");
    w.value(static_cast<std::int64_t>(s.series.paths.size()));
    w.key("readable_frames");
    w.value(static_cast<std::int64_t>(s.readable_frames));
    w.key("missing_reports");
    w.value(static_cast<std::int64_t>(s.missing_reports));
    w.key("usable_oecf_points");
    w.value(static_cast<std::int64_t>(s.usable_oecf_points));
    w.key("exif_consistent");
    w.value(s.exif_consistent);
    w.key("oecf_candidate");
    w.value(s.oecf_candidate);
    w.key("ptc_candidate");
    w.value(s.ptc_candidate);

    w.key("limitations");
    w.begin_array();
    for (const auto& limitation : s.limitations) w.value(limitation);
    w.end_array();

    w.key("points");
    w.begin_array();
    for (const auto& p : s.points) {
      w.begin_object();
      w.key("shutter_s");
      w.value(p.shutter_s);
      w.key("shutter_str");
      w.value(p.shutter_str);
      w.key("frame_count");
      w.value(static_cast<std::int64_t>(p.frames.size()));
      w.key("mean_signal_by_plane");
      write_double_array(w, p.mean_signal_by_plane);
      w.key("mean_spatial_stddev_by_plane");
      write_double_array(w, p.mean_spatial_stddev_by_plane);
      w.key("has_valid_signal_range");
      w.value(p.has_valid_signal_range);
      w.key("max_mean_fraction_of_range");
      w.value(p.max_mean_fraction_of_range);
      w.key("max_saturated_fraction");
      w.value(p.max_saturated_fraction);

      w.key("frames");
      w.begin_array();
      for (const auto& f : p.frames) {
        w.begin_object();
        w.key("path");
        w.value(f.path);
        w.key("shutter_s");
        w.value(f.shutter_s);
        w.key("shutter_str");
        w.value(f.shutter_str);
        w.key("planes");
        w.begin_array();
        for (const auto& plane : f.planes) write_plane_stats(w, plane);
        w.end_array();
        w.end_object();
      }
      w.end_array();
      w.end_object();
    }
    w.end_array();
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

}  // namespace camera_iq
