#include "camera_iq/commands.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "camera_iq/manifest.hpp"

namespace camera_iq {

int cmd_manifest(int argc, char** argv) {
  std::filesystem::path root;
  std::filesystem::path out;
  bool exif = true;
  std::size_t series_min = 3;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq manifest: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (arg == "--no-exif") {
      exif = false;
    } else if (arg == "--series-min") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq manifest: --series-min requires a number\n";
        return 2;
      }
      series_min = static_cast<std::size_t>(std::stoul(argv[++i]));
    } else if (root.empty()) {
      root = arg;
    } else {
      std::cerr << "camera_iq manifest: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }

  if (root.empty()) {
    std::cerr << "Usage: camera_iq manifest <dataset-root> [--out FILE]"
                 " [--no-exif] [--series-min N]\n";
    return 2;
  }

  try {
    auto entries = scan_dataset(root);
    std::cerr << "scanned " << entries.size() << " files under " << root
              << "\n";

    if (exif) {
      const auto populated = populate_raw_metadata(entries, root);
      std::size_t raw_total = 0;
      for (const auto& e : entries) {
        if (e.extension == "raf" || e.extension == "nef" ||
            e.extension == "arw" || e.extension == "cr2" ||
            e.extension == "iiq" || e.extension == "dng") {
          ++raw_total;
        }
      }
      std::cerr << "exif read for " << populated << "/" << raw_total
                << " raw files\n";
    }

    const auto series = find_exposure_series(entries, series_min);
    std::cerr << series.size() << " exposure-series candidates (>= "
              << series_min << " distinct shutters)\n";

    if (out.empty()) {
      write_manifest_json(std::cout, root.string(), entries, series);
      std::cout << "\n";
    } else {
      std::ofstream os(out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq manifest: cannot write " << out << "\n";
        return 1;
      }
      write_manifest_json(os, root.string(), entries, series);
      os << "\n";
      std::cerr << "manifest written to " << out << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq manifest: " << ex.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
