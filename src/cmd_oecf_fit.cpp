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
#include "camera_iq/oecf_fit.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {

int cmd_oecf_fit(int argc, char** argv) {
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
        std::cerr << "camera_iq oecf-fit: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq oecf-fit: --config requires a path\n";
        return 2;
      }
      config = argv[++i];
    } else if (arg == "--subdir") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq oecf-fit: --subdir requires a relative path\n";
        return 2;
      }
      subdir = argv[++i];
    } else if (arg == "--series-min") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq oecf-fit: --series-min requires a number\n";
        return 2;
      }
      series_min = static_cast<std::size_t>(std::stoul(argv[++i]));
    } else if (arg == "--series-limit") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq oecf-fit: --series-limit requires a number\n";
        return 2;
      }
      series_limit = static_cast<std::size_t>(std::stoul(argv[++i]));
    } else if (arg == "--roi") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq oecf-fit: --roi requires x,y,width,height\n";
        return 2;
      }
      roi = parse_roi_spec(argv[++i]);
      if (!roi) {
        std::cerr << "camera_iq oecf-fit: invalid --roi; expected "
                     "non-negative x,y and positive width,height\n";
        return 2;
      }
    } else if (root_or_id.empty()) {
      root_or_id = arg;
    } else {
      std::cerr << "camera_iq oecf-fit: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }

  if (root_or_id.empty()) {
    std::cerr << "Usage: camera_iq oecf-fit <dataset-root>"
                 " [--out FILE] [--config FILE] [--subdir REL]"
                 " [--series-min N] [--series-limit N]"
                 " [--roi x,y,width,height]\n";
    return 2;
  }

  try {
    const auto resolved = resolve_dataset_root(root_or_id, config);
    if (!resolved) {
      std::cerr << "camera_iq oecf-fit: '" << root_or_id
                << "' is not a directory or dataset id in " << config << "\n";
      return 1;
    }
    if (!subdir.empty() && subdir.is_absolute()) {
      std::cerr << "camera_iq oecf-fit: --subdir requires a relative path\n";
      return 2;
    }

    const std::filesystem::path scan_root =
        subdir.empty() ? resolved->root : (resolved->root / subdir);
    std::string root_label = resolved->label;
    if (resolved->from_config && !subdir.empty()) {
      root_label += "/";
      root_label += subdir.generic_string();
    } else if (!resolved->from_config) {
      root_label = scan_root.string();
    }

    auto entries = scan_dataset(scan_root);
    auto series = find_exposure_series(entries, series_min);
    if (series_limit > 0 && series.size() > series_limit) {
      series.resize(series_limit);
    }

    std::cerr << "scanned " << entries.size() << " files under " << root_label
              << "\n";
    std::cerr << series.size() << " exposure-response series selected (>= "
              << series_min << " distinct shutters)\n";

    std::vector<OecfSeriesFit> fits;
    fits.reserve(series.size());
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
                    << std::filesystem::path(rel).generic_string() << "\n";
        }
      }

      const auto summary = summarize_exposure_response(s, entries, reports);
      fits.push_back(fit_oecf_series(summary));
      const auto& fit = fits.back();
      std::cerr << "series " << (s.directory.empty() ? "." : s.directory)
                << " / " << s.group << ": " << summary.readable_frames << "/"
                << s.paths.size() << " readable frames, "
                << fit.usable_oecf_points << " usable points, fit "
                << (fit.fit_candidate ? "ready" : "not ready") << "\n";
    }

    if (out.empty()) {
      write_oecf_fit_json(std::cout, root_label, fits);
      std::cout << "\n";
    } else {
      std::ofstream os(out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq oecf-fit: cannot write " << out << "\n";
        return 1;
      }
      write_oecf_fit_json(os, root_label, fits);
      os << "\n";
      std::cerr << "OECF fit summary written to " << out << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq oecf-fit: " << ex.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
