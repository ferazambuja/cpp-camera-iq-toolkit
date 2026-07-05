#include "camera_iq/manifest.hpp"

#include <sstream>
#include <string>

#include "harness.hpp"

using camera_iq::find_exposure_series;
using camera_iq::ManifestEntry;
using camera_iq::write_manifest_json;
using test::check;

namespace {

bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}

ManifestEntry raf(const std::string& dir, const std::string& name) {
  ManifestEntry e;
  e.relative_path = dir + "/" + name;
  e.directory = dir;
  e.extension = "raf";
  e.size_bytes = 100;
  e.filename_meta = camera_iq::parse_capture_filename(name);
  return e;
}

}  // namespace

void TESTS() {
  std::vector<ManifestEntry> entries = {
      raf("Images/CCSG", "CCSG_f9.0_1:10_ISO200_DSCF0308.RAF"),
      raf("Images/CCSG", "CCSG_f9.0_1:100_ISO200_DSCF0299.RAF"),
      raf("Images/CCSG", "CCSG_f9.0_1:1000_ISO200_DSCF0290.RAF"),
  };
  ManifestEntry csv;
  csv.relative_path = "Images/coord.csv";
  csv.directory = "Images";
  csv.extension = "csv";
  csv.size_bytes = 42;
  csv.filesystem_mtime = "2026-07-04T12:34:56Z";
  csv.csv_shape = camera_iq::CsvShape{140, 4, false};
  entries.push_back(csv);

  // One entry with EXIF metadata attached.
  camera_iq::RawMeta rm;
  rm.make = "Fujifilm";
  rm.model = "X-T100";
  rm.iso = 200;
  rm.shutter_s = 0.1;
  rm.aperture = 9.0;
  rm.cfa_pattern = "RGGB";
  entries[0].raw_meta = rm;

  const auto series = find_exposure_series(entries, 3);

  std::ostringstream os;
  write_manifest_json(os, "test-root", entries, series);
  const std::string json = os.str();

  check(contains(json, "\"root\":\"test-root\""), "root label present");
  check(contains(json, "\"file_count\":4"), "file count");
  check(contains(json, "\"path\":\"Images/coord.csv\""), "csv path present");
  check(contains(json, "\"filesystem_mtime\":\"2026-07-04T12:34:56Z\""),
        "filesystem mtime serialized");
  check(contains(json, "\"rows\":140"), "csv rows serialized");
  check(contains(json, "\"model\":\"X-T100\""), "exif model serialized");
  check(contains(json, "\"cfa_pattern\":\"RGGB\""), "cfa serialized");
  check(contains(json, "\"exposure_series\""), "series section present");
  check(contains(json, "\"distinct_shutters\":3"), "series distinct count");
  check(contains(json, "\"shutter_str\":\"1:100\""), "filename shutter string");
  check(contains(json, "\"extension_counts\""), "summary extension counts");
  check(contains(json, "\"raf\":3"), "raf count in summary");

  // Entries without EXIF serialize exif as null, not a fabricated object.
  check(contains(json, "\"exif\":null"), "missing exif is null");

  // Output must be valid enough to end with a closing brace.
  check(!json.empty() && json.back() == '}', "document closed");
}
