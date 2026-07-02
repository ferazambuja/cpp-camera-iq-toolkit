#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "camera_iq/filename_meta.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {

// Shape of a CSV file, probed without interpreting the contents.
struct CsvShape {
  std::size_t rows = 0;            // data lines (trailing empty line ignored)
  std::size_t cols_first_row = 0;  // naive comma split of the first line
  bool header_guess = false;       // first row contains non-numeric fields
};

// One file in the dataset manifest.
struct ManifestEntry {
  std::string relative_path;  // forward slashes, relative to the scan root
  std::string directory;      // relative directory ("" for root-level files)
  std::string extension;      // lowercase, without the dot
  std::uintmax_t size_bytes = 0;
  FilenameMeta filename_meta;        // populated for capture files
  std::optional<CsvShape> csv_shape; // populated for .csv files
  std::optional<RawMeta> raw_meta;   // populated by populate_raw_metadata()
};

// Probes row/column shape of a CSV file. Throws std::runtime_error if the
// file cannot be opened.
CsvShape probe_csv(const std::filesystem::path& csv);

// Recursively enumerates all regular files under `root`, sorted by
// relative path. Hidden files (dotfiles, AppleDouble "._*") are skipped.
// Throws std::runtime_error if root does not exist or is not a directory.
std::vector<ManifestEntry> scan_dataset(const std::filesystem::path& root);

// A set of captures that differ only in shutter time — a candidate
// fixed-illumination exposure series (PTC/OECF feasibility input).
//
// Keyed strictly on (directory, filename group, aperture, ISO token).
// A missing ISO token is its own key value, NOT merged with explicit ISO:
// filename metadata alone cannot prove they match; the EXIF cross-check
// stage refines this.
struct ExposureSeries {
  std::string directory;
  std::string group;               // filename group token ("" if none)
  std::optional<double> aperture;
  std::optional<int> iso;
  std::vector<std::string> paths;  // relative paths, sorted
  std::size_t distinct_shutters = 0;
};

// Finds candidate exposure series among RAF entries whose filenames carry a
// shutter value. Series with fewer than `min_distinct` distinct shutter
// values are dropped. Output sorted by (directory, group, aperture).
std::vector<ExposureSeries> find_exposure_series(
    const std::vector<ManifestEntry>& entries, std::size_t min_distinct = 3);

// Reads LibRaw metadata for every RAW entry (by extension). Files LibRaw
// cannot parse keep raw_meta == nullopt. Returns the number populated.
std::size_t populate_raw_metadata(std::vector<ManifestEntry>& entries,
                                  const std::filesystem::path& root);

// Serializes the manifest (entries + exposure series + per-extension and
// per-directory summaries) as a JSON document.
void write_manifest_json(std::ostream& os, std::string_view root_label,
                         const std::vector<ManifestEntry>& entries,
                         const std::vector<ExposureSeries>& series);

}  // namespace camera_iq
