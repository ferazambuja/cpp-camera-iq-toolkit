#include "camera_iq/commands.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/dark_calibration.hpp"
#include "camera_iq/dataset_config.hpp"
#include "camera_iq/manifest.hpp"
#include "camera_iq/noise.hpp"
#include "camera_iq/raw_meta.hpp"
#include "camera_iq/roi.hpp"

namespace camera_iq {
namespace {

bool is_raw_extension(std::string_view ext) {
  return ext == "raf" || ext == "nef" || ext == "arw" || ext == "cr2" ||
         ext == "iiq" || ext == "dng";
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool looks_like_dark_entry(const ManifestEntry& entry) {
  std::string key = entry.directory;
  key += "/";
  key += entry.filename_meta.group.value_or("");
  key += "/";
  key += entry.relative_path;
  return lower(key).find("dark") != std::string::npos;
}

std::optional<double> parse_nonnegative_double(std::string_view text) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(std::string(text), &consumed);
    if (consumed != text.size() || value < 0.0) return std::nullopt;
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

struct AcceptedFrame {
  ManifestEntry entry;
  DarkCalibrationFrame calibration;
  RawCfaReport report;
  std::optional<int> effective_iso;
};

std::string group_key(const AcceptedFrame& frame) {
  std::ostringstream os;
  os << frame.entry.filename_meta.aperture.value_or(-1.0) << "|"
     << frame.entry.filename_meta.shutter_s.value_or(-1.0) << "|"
     << frame.effective_iso.value_or(-1);
  return os.str();
}

void add_exclusion(NoiseSummary& summary, std::string path, std::string reason,
                   double max_abs_mean_residual = 0.0) {
  summary.exclusions.push_back(
      NoiseExcludedFrame{std::move(path), std::move(reason),
                         max_abs_mean_residual});
}

}  // namespace

int cmd_noise(int argc, char** argv) {
  std::string root_or_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path subdir;
  std::filesystem::path out;
  std::optional<RoiRect> requested_roi;
  double residual_tolerance_dn = 2.0;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq noise: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq noise: --config requires a path\n";
        return 2;
      }
      config = argv[++i];
    } else if (arg == "--subdir") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq noise: --subdir requires a relative path\n";
        return 2;
      }
      subdir = argv[++i];
    } else if (arg == "--roi") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq noise: --roi requires x,y,width,height\n";
        return 2;
      }
      requested_roi = parse_roi_spec(argv[++i]);
      if (!requested_roi) {
        std::cerr << "camera_iq noise: invalid --roi; expected non-negative "
                     "x,y and positive width,height\n";
        return 2;
      }
    } else if (arg == "--residual-tolerance") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq noise: --residual-tolerance requires a DN value\n";
        return 2;
      }
      const auto parsed = parse_nonnegative_double(argv[++i]);
      if (!parsed) {
        std::cerr << "camera_iq noise: --residual-tolerance requires a "
                     "non-negative numeric DN value\n";
        return 2;
      }
      residual_tolerance_dn = *parsed;
    } else if (root_or_id.empty()) {
      root_or_id = arg;
    } else {
      std::cerr << "camera_iq noise: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }

  if (root_or_id.empty()) {
    std::cerr << "Usage: camera_iq noise <dataset-root> [--out FILE] "
                 "[--config FILE] [--subdir REL] [--roi x,y,width,height] "
                 "[--residual-tolerance DN]\n";
    return 2;
  }

  try {
    const auto resolved = resolve_dataset_root(root_or_id, config);
    if (!resolved) {
      std::cerr << "camera_iq noise: '" << root_or_id
                << "' is not a directory or dataset id in " << config << "\n";
      return 1;
    }
    if (!subdir.empty() && !is_safe_dataset_subdir(subdir)) {
      std::cerr << "camera_iq noise: --subdir requires a relative path inside the dataset root\n";
      return 2;
    }

    const std::filesystem::path scan_root =
        subdir.empty() ? resolved->root : (resolved->root / subdir);
    const std::string root_label = dataset_scan_label(*resolved, subdir);

    const auto scanned = scan_dataset(scan_root);
    std::vector<ManifestEntry> entries;
    for (const auto& entry : scanned) {
      if (!is_raw_extension(entry.extension)) continue;
      if (subdir.empty() && !looks_like_dark_entry(entry)) continue;
      entries.push_back(entry);
    }

    std::cerr << "selected " << entries.size() << " dark-frame candidates under "
              << root_label << "\n";

    std::map<std::string, RawCfaReport> reports;
    for (const auto& entry : entries) {
      const auto report = read_raw_cfa_stats(scan_root / entry.relative_path);
      if (report) {
        reports.emplace(entry.relative_path, *report);
      } else {
        std::cerr << "warning: could not read/unpack " << root_label << "/"
                  << std::filesystem::path(entry.relative_path).generic_string()
                  << "\n";
      }
    }

    const auto calibration =
        summarize_dark_calibration(entries, reports, residual_tolerance_dn);

    NoiseSummary summary;
    summary.root_label = root_label;
    summary.candidate_frames = calibration.candidate_frames;
    summary.readable_frames = calibration.readable_frames;
    summary.in_tolerance_frames = calibration.frames_within_tolerance;
    summary.gain_candidate = false;
    summary.ptc_candidate = false;
    summary.dr_candidate = false;
    summary.gain_not_supported_reason =
        "no_uniform_flat_exposure_stack_for_system_gain";
    summary.ptc_not_supported_reason =
        "no_repeated_uniform_flat_ladder_for_photon_transfer";
    summary.dr_not_supported_reason =
        "requires_system_gain_and_full_well_after_photon_transfer";
    summary.limitations.push_back(
        "DN-only temporal dark-frame analysis: no electron conversion, PTC, "
        "full-well, or engineering dynamic range is computed.");
    summary.limitations.push_back(
        "Temporal noise approximates read noise only when dark current is "
        "negligible over the selected shutter span.");
    summary.limitations.push_back(
        "Moment DSNU is defect-pixel-inclusive; robust MAD companion is "
        "reported to expose hot-pixel sensitivity.");

    std::map<std::string, ManifestEntry> entries_by_path;
    for (const auto& entry : entries) entries_by_path.emplace(entry.relative_path, entry);

    for (const auto& entry : entries) {
      if (reports.find(entry.relative_path) == reports.end()) {
        add_exclusion(summary, entry.relative_path, "raw_unreadable");
      }
    }

    std::vector<AcceptedFrame> accepted;
    accepted.reserve(calibration.frames.size());
    for (const auto& frame : calibration.frames) {
      const auto entry_it = entries_by_path.find(frame.path);
      const auto report_it = reports.find(frame.path);
      if (entry_it == entries_by_path.end() || report_it == reports.end()) {
        continue;
      }
      const ManifestEntry& entry = entry_it->second;
      if (!frame.within_residual_tolerance) {
        add_exclusion(summary, frame.path, "dark_calibration_outlier",
                      frame.max_abs_mean_residual);
        continue;
      }
      if (!entry.filename_meta.shutter_s || !entry.filename_meta.aperture) {
        add_exclusion(summary, frame.path,
                      "unpairable_missing_filename_exposure");
        continue;
      }
      const RawCfaReport& report = report_it->second;
      std::optional<int> effective_iso = entry.filename_meta.iso;
      if (report.meta.iso > 0.0) {
        const int exif_iso = static_cast<int>(std::lround(report.meta.iso));
        if (entry.filename_meta.iso && *entry.filename_meta.iso != exif_iso) {
          add_exclusion(summary, frame.path, "filename_exif_iso_mismatch",
                        frame.max_abs_mean_residual);
          continue;
        }
        if (!effective_iso) effective_iso = exif_iso;
      }
      accepted.push_back(AcceptedFrame{entry, frame, report, effective_iso});
    }

    std::map<std::string, std::vector<AcceptedFrame>> groups;
    for (const auto& frame : accepted) groups[group_key(frame)].push_back(frame);
    for (auto& [key, frames] : groups) {
      (void)key;
      std::sort(frames.begin(), frames.end(),
                [](const AcceptedFrame& a, const AcceptedFrame& b) {
                  return a.entry.relative_path < b.entry.relative_path;
                });
      if (frames.size() < 2) {
        add_exclusion(summary, frames.front().entry.relative_path,
                      "singleton_unpairable",
                      frames.front().calibration.max_abs_mean_residual);
        continue;
      }

      const AcceptedFrame& a = frames[0];
      const AcceptedFrame& b = frames[1];
      const auto image_a = read_raw_cfa_image(scan_root / a.entry.relative_path);
      const auto image_b = read_raw_cfa_image(scan_root / b.entry.relative_path);
      if (!image_a || !image_b) {
        add_exclusion(summary, a.entry.relative_path, "pair_raw_unreadable",
                      a.calibration.max_abs_mean_residual);
        add_exclusion(summary, b.entry.relative_path, "pair_raw_unreadable",
                      b.calibration.max_abs_mean_residual);
        continue;
      }
      std::optional<RoiRect> actual_roi;
      if (requested_roi) {
        actual_roi = cfa_balanced_roi(*requested_roi, image_a->width,
                                      image_a->height);
        if (!actual_roi) {
          add_exclusion(summary, a.entry.relative_path, "invalid_noise_roi",
                        a.calibration.max_abs_mean_residual);
          add_exclusion(summary, b.entry.relative_path, "invalid_noise_roi",
                        b.calibration.max_abs_mean_residual);
          continue;
        }
      }
      if (const auto err = validate_noise_pair_compatibility(*image_a, *image_b)) {
        add_exclusion(summary, a.entry.relative_path, *err,
                      a.calibration.max_abs_mean_residual);
        add_exclusion(summary, b.entry.relative_path, *err,
                      b.calibration.max_abs_mean_residual);
        continue;
      }
      summary.pairs.push_back(compute_noise_pair_estimate(
          *image_a, *image_b, a.entry.relative_path, b.entry.relative_path,
          a.entry.filename_meta.shutter_str.value_or(""),
          a.entry.filename_meta.shutter_s.value_or(0.0), a.effective_iso,
          a.entry.filename_meta.aperture, actual_roi));
      for (std::size_t i = 2; i < frames.size(); ++i) {
        add_exclusion(summary, frames[i].entry.relative_path,
                      "extra_replicate_not_used",
                      frames[i].calibration.max_abs_mean_residual);
      }
    }

    summary.matched_pair_count = summary.pairs.size();
    summary.single_pair_only = summary.matched_pair_count == 1;
    summary.excluded_frames = summary.exclusions.size();

    std::vector<std::pair<double, std::array<double, 4>>> dark_points;
    dark_points.reserve(accepted.size());
    std::array<std::string, 4> labels{"R", "G1", "B", "G2"};
    bool labels_set = false;
    for (const auto& frame : accepted) {
      dark_points.push_back({frame.entry.filename_meta.shutter_s.value_or(0.0),
                             frame.calibration.mean_residual_by_plane});
      if (!labels_set) {
        for (std::size_t p = 0; p < labels.size(); ++p) {
          labels[p] = frame.report.planes[p].label;
        }
        labels_set = true;
      }
    }
    summary.dark_current_fits =
        fit_dark_current_diagnostic(dark_points, labels);

    if (summary.pairs.empty()) {
      summary.limitations.push_back(
          "No in-tolerance matched dark-frame pair survived the quality gates.");
    } else if (summary.single_pair_only) {
      summary.limitations.push_back(
          "Only one clean matched dark-frame pair survived; no independent "
          "pair-level cross-check is claimed.");
    }

    if (out.empty()) {
      write_noise_json(std::cout, summary);
      std::cout << "\n";
    } else {
      std::ofstream os(out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq noise: cannot write " << out << "\n";
        return 1;
      }
      write_noise_json(os, summary);
      os << "\n";
      std::cerr << "noise summary written to " << out << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq noise: " << ex.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
