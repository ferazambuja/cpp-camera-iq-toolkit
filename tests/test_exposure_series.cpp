#include "camera_iq/manifest.hpp"

#include <cctype>

#include "harness.hpp"

using camera_iq::find_exposure_series;
using camera_iq::ManifestEntry;
using test::check;

namespace {

ManifestEntry raf(const std::string& dir, const std::string& name) {
  ManifestEntry e;
  e.relative_path = dir + "/" + name;
  e.directory = dir;
  const size_t dot = name.rfind('.');
  e.extension = dot == std::string::npos ? "" : name.substr(dot + 1);
  for (char& c : e.extension) c = static_cast<char>(std::tolower(
      static_cast<unsigned char>(c)));
  e.filename_meta = camera_iq::parse_capture_filename(name);
  return e;
}

}  // namespace

void TESTS() {
  std::vector<ManifestEntry> entries = {
      raf("Images/CCSG", "CCSG_f9.0_1:10_ISO200_DSCF0308.RAF"),
      raf("Images/CCSG", "CCSG_f9.0_1:100_ISO200_DSCF0299.RAF"),
      raf("Images/CCSG", "CCSG_f9.0_1:1000_ISO200_DSCF0290.RAF"),
      raf("Images/CCSG", "CCSG_f9.0_1:15_ISO200_DSCF0306.RAF"),
      // Duplicate shutter — counted once in distinct_shutters.
      raf("Images/CCSG", "CCSG_f9.0_1:1000_ISO200_DSCF0291.RAF"),
      // Different aperture — separate series, below threshold.
      raf("Images/CCSG", "CCSG_f5.6_1:10_ISO200_DSCF0350.RAF"),
      raf("Images/CCSG", "CCSG_f5.6_1:20_ISO200_DSCF0351.RAF"),
      // No ISO token — separate series from the ISO200 one.
      raf("Images/Dark Frame", "Dark_Frame_f8.0_1:10_DSCF0439.RAF"),
      raf("Images/Dark Frame", "Dark_Frame_f8.0_1:20_DSCF0440.RAF"),
      raf("Images/Dark Frame", "Dark_Frame_f8.0_1:40_DSCF0441.RAF"),
      // Non-RAF RAW files are valid series candidates when their filenames
      // carry the same exposure tokens.
      raf("Archive/NEF", "Sphere_f8.0_1:100_DSCF0001.NEF"),
      raf("Archive/NEF", "Sphere_f8.0_1:50_DSCF0002.NEF"),
      raf("Archive/NEF", "Sphere_f8.0_1:25_DSCF0003.NEF"),
      // Plain name, no shutter — ignored entirely.
      raf("1st Try", "DSCF0193.RAF"),
  };

  const auto series = find_exposure_series(entries, 3);

  check(series.size() == 3, "three series above threshold");

  if (series.size() == 3) {
    // "Archive/NEF" < "Images/...", so the NEF series sorts first.
    const auto& nef = series[0];
    check(nef.directory == "Archive/NEF", "nef series present");
    check(nef.group == "Sphere", "nef group");
    check(nef.distinct_shutters == 3, "nef distinct shutters");

    const auto& dark = series[2];
    check(dark.directory == "Images/Dark Frame", "sorted: dark frame third");
    check(dark.group == "Dark_Frame", "dark group");
    check(!dark.iso.has_value(), "dark iso token absent");
    check(dark.distinct_shutters == 3, "dark distinct shutters");
    check(dark.paths.size() == 3, "dark path count");

    const auto& ccsg = series[1];
    check(ccsg.directory == "Images/CCSG", "ccsg series present");
    check(ccsg.group == "CCSG", "ccsg group");
    check(ccsg.aperture.has_value() && *ccsg.aperture == 9.0, "ccsg aperture");
    check(ccsg.iso == 200, "ccsg iso");
    check(ccsg.distinct_shutters == 4, "duplicate shutter counted once");
    check(ccsg.paths.size() == 5, "all five frames listed");
    bool sorted = true;
    for (size_t i = 1; i < ccsg.paths.size(); ++i) {
      if (ccsg.paths[i - 1] >= ccsg.paths[i]) sorted = false;
    }
    check(sorted, "paths sorted");
  }

  const auto all = find_exposure_series(entries, 2);
  check(all.size() == 4, "min_distinct=2 admits the f5.6 pair");
}
