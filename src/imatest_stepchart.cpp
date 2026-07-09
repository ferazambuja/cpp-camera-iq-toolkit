#include "camera_iq/imatest_stepchart.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera_iq/csv.hpp"

namespace camera_iq {
namespace {

[[noreturn]] void fail(const std::string& message) {
  throw std::runtime_error("Imatest Stepchart: " + message);
}

double require_double(const std::string& text, const std::string& context) {
  const auto value = parse_double(text);
  if (!value) fail("invalid " + context + ": '" + text + "'");
  return *value;
}

int require_int(const std::string& text, const std::string& context) {
  const auto value = parse_int(text);
  if (!value) fail("invalid " + context + ": '" + text + "'");
  return *value;
}

bool is_primary_header(const std::vector<std::string>& cells) {
  static const char* kExpected[] = {
      "Zone",       "Pixel",     "Pixel/255", "Log(exp)",
      "Log(px/255)", "Width px", "Height px", "Pixels total"};
  if (cells.size() < std::size(kExpected)) return false;
  for (std::size_t i = 0; i < std::size(kExpected); ++i) {
    if (cells[i] != kExpected[i]) return false;
  }
  return true;
}

bool looks_like_stepchart_header(const std::vector<std::string>& cells) {
  // The Imatest version is captured as per-summary data, never pinned here:
  // fusing a version equality into format detection would reject genuine
  // Stepchart summaries from other builds with a misleading reason.
  return cells.size() >= 4 && cells[0] == "Imatest" && !cells[1].empty() &&
         cells[3] == "Stepchart";
}

int parse_file_count_line(const std::string& line) {
  static const std::regex kPattern(R"(^\s*(\d+)\s+files combined for analysis\.\s*$)");
  std::smatch m;
  if (!std::regex_match(line, m, kPattern)) return 0;
  const auto value = parse_int(m[1].str());
  if (!value || *value <= 0) fail("invalid file-count line: '" + line + "'");
  return *value;
}

// Returns the N of a "File N" key, or nullopt for non-file-list rows
// (including the Exif decoys "File Name"/"File Size"/"File Source").
std::optional<int> combined_file_row_index(const std::string& key) {
  static const std::regex kPattern(R"(^File\s+(\d+)$)");
  std::smatch m;
  if (!std::regex_match(key, m, kPattern)) return std::nullopt;
  const auto value = parse_int(m[1].str());
  if (!value || *value <= 0) fail("invalid file-list index: '" + key + "'");
  return value;
}

void validate_combined_file_name(const std::string& filename) {
  if (filename.find('/') != std::string::npos ||
      filename.find('\\') != std::string::npos ||
      std::filesystem::path(filename).has_parent_path()) {
    fail("invalid file-list filename: '" + filename + "'");
  }
}

void validate_zones(const std::vector<ImatestStepchartZone>& zones) {
  if (zones.size() < 2) fail("N < 2");
  for (std::size_t i = 0; i < zones.size(); ++i) {
    const int expected_zone = static_cast<int>(i + 1);
    if (zones[i].zone != expected_zone) {
      fail("missing or non-contiguous zone rows");
    }
    if (zones[i].pixel < 0.0) fail("negative Pixel");
    if (zones[i].pixel_255 < 0.0) fail("negative Pixel/255");
    if (zones[i].width_px <= 0 || zones[i].height_px <= 0 ||
        zones[i].pixels_total <= 0) {
      fail("zero or negative geometry");
    }
  }
  for (std::size_t i = 0; i + 1 < zones.size(); ++i) {
    if (!(zones[i].log_exposure > zones[i + 1].log_exposure)) {
      fail("Log(exp) is not strictly decreasing");
    }
    if (zones[i].pixel < zones[i + 1].pixel) {
      fail("pixel monotonic reversal");
    }
  }
  if (!(zones.front().pixel > zones.back().pixel)) {
    fail("pixel endpoint check failed");
  }
}

}  // namespace

ImatestStepchartSummary read_imatest_stepchart_summary(
    const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) fail("cannot open " + path.string());

  ImatestStepchartSummary summary;
  summary.summary_file_basename = path.filename().string();

  bool saw_stepchart = false;
  bool in_primary = false;
  bool saw_primary_header = false;
  int expected_next_zone = 1;

  std::string line;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto cells = split_csv_line(line);
    if (cells.empty() || (cells.size() == 1 && cells[0].empty())) {
      if (in_primary) in_primary = false;
      continue;
    }

    if (!saw_stepchart && looks_like_stepchart_header(cells)) {
      saw_stepchart = true;
      summary.imatest_version = cells[1];
      continue;
    }

    const std::string& key = cells[0];
    if (key == "Run date") {
      if (cells.size() < 2 || cells[1].empty()) fail("missing run date");
      summary.run_date = cells[1];
      continue;
    }
    if (key == "File name") {
      if (cells.size() < 2 || cells[1].empty()) fail("missing file name");
      summary.file_name = cells[1];
      continue;
    }
    if (key == "Zones") {
      if (cells.size() < 2) fail("missing Zones,N value");
      summary.declared_zone_count = require_int(cells[1], "Zones,N");
      if (summary.declared_zone_count < 2) fail("N < 2");
      continue;
    }

    const int declared_count = parse_file_count_line(line);
    if (declared_count > 0) {
      summary.declared_file_count = declared_count;
      continue;
    }
    if (const auto file_index = combined_file_row_index(key)) {
      if (cells.size() < 2 || cells[1].empty()) fail("missing file-list row");
      validate_combined_file_name(cells[1]);
      // The row index must be the sequence 1..M — a duplicated or skipped
      // index with an intact count would otherwise be silently trusted.
      if (*file_index != static_cast<int>(summary.combined_files.size()) + 1) {
        fail("missing or non-contiguous file-list rows");
      }
      summary.combined_files.push_back(cells[1]);
      continue;
    }

    if (is_primary_header(cells)) {
      if (saw_primary_header) fail("duplicate primary table");
      saw_primary_header = true;
      in_primary = true;
      expected_next_zone = 1;
      continue;
    }

    if (!in_primary) continue;
    if (cells.size() != 8) {
      in_primary = false;
      continue;
    }
    if (summary.declared_zone_count > 0 &&
        static_cast<int>(summary.zones.size()) >= summary.declared_zone_count) {
      fail("primary table has more rows than Zones,N");
    }

    ImatestStepchartZone z;
    z.zone = require_int(cells[0], "zone");
    if (z.zone != expected_next_zone) fail("missing or non-contiguous zone rows");
    ++expected_next_zone;
    z.pixel = require_double(cells[1], "Pixel");
    z.pixel_255 = require_double(cells[2], "Pixel/255");
    z.log_exposure = require_double(cells[3], "Log(exp)");
    z.log_pixel_255 = require_double(cells[4], "Log(px/255)");
    z.width_px = require_int(cells[5], "Width px");
    z.height_px = require_int(cells[6], "Height px");
    z.pixels_total = require_int(cells[7], "Pixels total");
    summary.zones.push_back(z);
  }

  if (!saw_stepchart) fail("not a Stepchart CSV");
  if (summary.run_date.empty()) fail("missing run date");
  if (summary.file_name.empty()) fail("missing file name");
  if (summary.declared_file_count <= 0) fail("missing file-count line");
  if (static_cast<int>(summary.combined_files.size()) !=
      summary.declared_file_count) {
    fail("file-list count mismatch");
  }
  if (summary.declared_zone_count <= 0) fail("missing Zones,N");
  if (static_cast<int>(summary.zones.size()) != summary.declared_zone_count) {
    fail("primary row count mismatch with Zones,N");
  }
  validate_zones(summary.zones);
  return summary;
}

}  // namespace camera_iq
