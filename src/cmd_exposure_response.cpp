#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/dataset_config.hpp"
#include "camera_iq/exposure_response.hpp"
#include "camera_iq/manifest.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {

int cmd_exposure_response(int argc, char** argv) {
  std::string root_or_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path subdir;
  std::filesystem::path out;
  std::optional<RoiRect> roi;
  std::size_t series_min = 3;
  std::size_t series_limit = 0;  // 0 means all detected series.

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq exposure-response: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq exposure-response: --config requires a path\n";
        return 2;
      }
      config = argv[++i];
    } else if (arg == "--subdir") {
      if (i + 1 >= argc) {
        std::cerr
            << "camera_iq exposure-response: --subdir requires a relative path\n";
        return 2;
      }
      subdir = argv[++i];
    } else if (arg == "--series-min") {
      if (i + 1 >= argc) {
        std::cerr
            << "camera_iq exposure-response: --series-min requires a number\n";
        return 2;
      }
      series_min = static_cast<std::size_t>(std::stoul(argv[++i]));
    } else if (arg == "--series-limit") {
      if (i + 1 >= argc) {
        std::cerr
            << "camera_iq exposure-response: --series-limit requires a number\n";
        return 2;
      }
      series_limit = static_cast<std::size_t>(std::stoul(argv[++i]));
    } else if (arg == "--roi") {
      if (i + 1 >= argc) {
        std::cerr
            << "camera_iq exposure-response: --roi requires x,y,width,height\n";
        return 2;
      }
      roi = parse_roi_spec(argv[++i]);
      if (!roi) {
        std::cerr << "camera_iq exposure-response: invalid --roi; expected "
                     "non-negative x,y and positive width,height\n";
        return 2;
      }
    } else if (root_or_id.empty()) {
      root_or_id = arg;
    } else {
      std::cerr << "camera_iq exposure-response: unexpected argument '" << arg
                << "'\n";
      return 2;
    }
  }

  if (root_or_id.empty()) {
    std::cerr << "Usage: camera_iq exposure-response <dataset-root>"
                 " [--out FILE] [--config FILE] [--subdir REL]"
                 " [--series-min N] [--series-limit N]"
                 " [--roi x,y,width,height]\n";
    return 2;
  }

  try {
    const auto resolved = resolve_dataset_root(root_or_id, config);
    if (!resolved) {
      std::cerr << "camera_iq exposure-response: '" << root_or_id
                << "' is not a directory or dataset id in " << config << "\n";
      return 1;
    }
    if (!subdir.empty() && subdir.is_absolute()) {
      std::cerr
          << "camera_iq exposure-response: --subdir requires a relative path\n";
      return 2;
    }
    const std::filesystem::path scan_root = subdir.empty()
        ? resolved->root
        : (resolved->root / subdir);
    const std::string root_label = dataset_scan_label(*resolved, subdir);

    auto entries = scan_dataset(scan_root);
    auto series = find_exposure_series(entries, series_min);
    if (series_limit > 0 && series.size() > series_limit) {
      series.resize(series_limit);
    }

    std::cerr << "scanned " << entries.size() << " files under " << root_label
              << "\n";
    std::cerr << series.size() << " exposure-response series selected (>= "
              << series_min << " distinct shutters)\n";

    std::vector<ExposureResponseSummary> summaries;
    summaries.reserve(series.size());
    for (const auto& s : series) {
      std::map<std::string, RawCfaReport> reports;
      for (const auto& rel : s.paths) {
        std::optional<RawCfaReport> report;
        if (roi) {
          const auto image = read_raw_cfa_image(scan_root / rel);
          if (image) report = raw_cfa_report_for_roi(*image, *roi);
        } else {
          report = read_raw_cfa_stats(scan_root / rel);
        }
        if (report) {
          reports.emplace(rel, *report);
        } else {
          std::cerr << "warning: could not read/unpack " << root_label << "/"
                    << std::filesystem::path(rel).generic_string()
                    << "\n";
        }
      }
      summaries.push_back(summarize_exposure_response(s, entries, reports));
      const auto& summary = summaries.back();
      std::cerr << "series " << (s.directory.empty() ? "." : s.directory)
                << " / " << s.group << ": " << summary.readable_frames << "/"
                << s.paths.size() << " readable frames\n";
    }

    if (out.empty()) {
      write_exposure_response_json(std::cout, root_label, summaries);
      std::cout << "\n";
    } else {
      std::ofstream os(out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq exposure-response: cannot write " << out
                  << "\n";
        return 1;
      }
      write_exposure_response_json(os, root_label, summaries);
      os << "\n";
      std::cerr << "exposure-response summary written to " << out << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq exposure-response: " << ex.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
