#include "camera_iq/commands.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/dark_calibration.hpp"
#include "camera_iq/dataset_config.hpp"
#include "camera_iq/manifest.hpp"
#include "camera_iq/raw_meta.hpp"

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

}  // namespace

int cmd_dark_calibration(int argc, char** argv) {
  std::string root_or_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path subdir;
  std::filesystem::path out;
  double residual_tolerance_dn = 2.0;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq dark-calibration: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq dark-calibration: --config requires a path\n";
        return 2;
      }
      config = argv[++i];
    } else if (arg == "--subdir") {
      if (i + 1 >= argc) {
        std::cerr
            << "camera_iq dark-calibration: --subdir requires a relative path\n";
        return 2;
      }
      subdir = argv[++i];
    } else if (arg == "--residual-tolerance") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq dark-calibration: --residual-tolerance "
                     "requires a DN value\n";
        return 2;
      }
      const std::string value = argv[++i];
      try {
        std::size_t consumed = 0;
        residual_tolerance_dn = std::stod(value, &consumed);
        if (consumed != value.size()) {
          std::cerr << "camera_iq dark-calibration: --residual-tolerance "
                       "requires a numeric DN value\n";
          return 2;
        }
      } catch (const std::exception&) {
        std::cerr << "camera_iq dark-calibration: --residual-tolerance "
                     "requires a numeric DN value\n";
        return 2;
      }
      if (residual_tolerance_dn < 0.0) {
        std::cerr << "camera_iq dark-calibration: --residual-tolerance must be "
                     "non-negative\n";
        return 2;
      }
    } else if (root_or_id.empty()) {
      root_or_id = arg;
    } else {
      std::cerr << "camera_iq dark-calibration: unexpected argument '" << arg
                << "'\n";
      return 2;
    }
  }

  if (root_or_id.empty()) {
    std::cerr << "Usage: camera_iq dark-calibration <dataset-root>"
                 " [--out FILE] [--config FILE] [--subdir REL]"
                 " [--residual-tolerance DN]\n";
    return 2;
  }

  try {
    const auto resolved = resolve_dataset_root(root_or_id, config);
    if (!resolved) {
      std::cerr << "camera_iq dark-calibration: '" << root_or_id
                << "' is not a directory or dataset id in " << config << "\n";
      return 1;
    }
    if (!subdir.empty() && !is_safe_dataset_subdir(subdir)) {
      std::cerr
          << "camera_iq dark-calibration: --subdir requires a relative path inside the dataset root\n";
      return 2;
    }

    const std::filesystem::path scan_root =
        subdir.empty() ? resolved->root : (resolved->root / subdir);
    const std::string root_label = dataset_scan_label(*resolved, subdir);

    const auto scanned = scan_dataset(scan_root);
    std::vector<ManifestEntry> entries;
    entries.reserve(scanned.size());
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

    const auto summary =
        summarize_dark_calibration(entries, reports, residual_tolerance_dn);

    if (out.empty()) {
      write_dark_calibration_json(std::cout, root_label, summary);
      std::cout << "\n";
    } else {
      std::ofstream os(out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq dark-calibration: cannot write " << out
                  << "\n";
        return 1;
      }
      write_dark_calibration_json(os, root_label, summary);
      os << "\n";
      std::cerr << "dark-calibration summary written to " << out << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq dark-calibration: " << ex.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
