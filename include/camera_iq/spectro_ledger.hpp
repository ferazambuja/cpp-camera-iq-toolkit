#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace camera_iq {

// One canonical reading declared by data/spectro_identity_ledger.csv.
struct SpectroLedgerEntry {
  std::string group_id;
  std::size_t repeat_index = 0;
  std::string canonical_path;
  std::string sha256;
  // Byte-identical copies of the same reading under different names. They carry
  // the scene and repeat labels the numbered canonical files do not, which is
  // how the ledger derives identity without trusting directory order.
  std::vector<std::string> alias_paths;
};

// The canonical readings of one repeat group, in repeat order.
struct SpectroLedgerGroup {
  std::string group_id;
  std::vector<SpectroLedgerEntry> readings;
};

// Parses the committed identity ledger into repeat groups.
//
// Grouping a measurement archive by directory order silently mislabels it the
// moment a file is missing or renamed, and this archive has both: three of its
// 24 scenes are missing a second capture, and its canonical files are numbered
// by acquisition order rather than by scene. The ledger states the grouping
// instead, so this parser refuses anything it cannot read unambiguously rather
// than falling back on position.
//
// Refuses, with std::runtime_error naming the cause: a header that is not the
// committed schema, a row without five fields, a repeat index that is not
// exactly 1..n within its group, a group whose rows are not contiguous, a
// canonical path or content digest that repeats, a digest that is not 64
// lowercase hexadecimal digits, and any path that is not source-relative.
std::vector<SpectroLedgerGroup> read_spectro_ledger(const std::string& csv_text);

}  // namespace camera_iq
