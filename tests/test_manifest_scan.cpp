#include "camera_iq/manifest.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "harness.hpp"

namespace fs = std::filesystem;
using camera_iq::probe_csv;
using camera_iq::scan_dataset;
using test::check;

namespace {

void write_file(const fs::path& p, const std::string& content) {
  fs::create_directories(p.parent_path());
  std::ofstream os(p, std::ios::binary);
  os << content;
}

const camera_iq::ManifestEntry* find_entry(
    const std::vector<camera_iq::ManifestEntry>& entries,
    const std::string& relative_path) {
  for (const auto& e : entries) {
    if (e.relative_path == relative_path) return &e;
  }
  return nullptr;
}

}  // namespace

void TESTS() {
  const fs::path root =
      fs::temp_directory_path() / "camera_iq_test_dataset";
  fs::remove_all(root);

  write_file(root / "Images/CCSG/CCSG_f9.0_1:100_ISO200_DSCF0299.RAF", "rafdata");
  write_file(root / "Images/Dark Frame/Dark_Frame_f8.0_1:10_DSCF0439.RAF", "x");
  write_file(root / "Images/coord.csv", "1.5,2.5,70,70\n3.5,4.5,70,70\n");
  write_file(root / "Images/stats.csv",
             "\"Filename\",\"ISO\",\"Ravg\"\n\"a.RAF\",200,4139.45\n");
  write_file(root / "notes.txt", "hello");
  write_file(root / ".DS_Store", "junk");
  write_file(root / "Images/._CCSG_appledouble", "junk");

  const auto entries = scan_dataset(root);

  check(entries.size() == 5, "hidden files excluded, five entries");
  check(find_entry(entries, ".DS_Store") == nullptr, "no .DS_Store");
  check(find_entry(entries, "Images/._CCSG_appledouble") == nullptr,
        "no AppleDouble");

  // Sorted by relative path.
  bool sorted = true;
  for (size_t i = 1; i < entries.size(); ++i) {
    if (entries[i - 1].relative_path >= entries[i].relative_path) sorted = false;
  }
  check(sorted, "entries sorted by relative path");

  const auto* raf =
      find_entry(entries, "Images/CCSG/CCSG_f9.0_1:100_ISO200_DSCF0299.RAF");
  check(raf != nullptr, "RAF entry present");
  if (raf) {
    check(raf->directory == "Images/CCSG", "RAF directory");
    check(raf->extension == "raf", "extension lowercased");
    check(raf->size_bytes == 7, "size recorded");
    check(raf->filename_meta.group == "CCSG", "filename meta populated");
    check(!raf->csv_shape.has_value(), "no csv shape on RAF");
  }

  const auto* dark =
      find_entry(entries, "Images/Dark Frame/Dark_Frame_f8.0_1:10_DSCF0439.RAF");
  check(dark != nullptr, "entry in directory with space");

  const auto* coord = find_entry(entries, "Images/coord.csv");
  check(coord != nullptr && coord->csv_shape.has_value(), "csv shape probed");
  if (coord && coord->csv_shape) {
    check(coord->csv_shape->rows == 2, "coord rows");
    check(coord->csv_shape->cols_first_row == 4, "coord cols");
    check(!coord->csv_shape->header_guess, "numeric csv: no header guess");
  }

  const auto* stats = find_entry(entries, "Images/stats.csv");
  if (stats && stats->csv_shape) {
    check(stats->csv_shape->rows == 2, "stats rows");
    check(stats->csv_shape->cols_first_row == 3, "stats cols");
    check(stats->csv_shape->header_guess, "text csv: header guessed");
  } else {
    check(false, "stats csv probed");
  }

  const auto* txt = find_entry(entries, "notes.txt");
  check(txt != nullptr && txt->directory.empty(), "root-level file, empty dir");

  bool threw = false;
  try {
    scan_dataset(root / "does_not_exist");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "missing root throws");

  fs::remove_all(root);
}
