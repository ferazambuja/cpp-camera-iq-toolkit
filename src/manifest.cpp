#include "camera_iq/manifest.hpp"

#include "camera_iq/json_writer.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace camera_iq {
namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

bool is_hidden(const std::filesystem::path& p) {
  const std::string name = p.filename().string();
  return !name.empty() && name.front() == '.';
}

bool looks_numeric(std::string_view field) {
  if (field.empty()) return true;  // empty field: not evidence of a header
  try {
    size_t consumed = 0;
    (void)std::stod(std::string(field), &consumed);
    return consumed == field.size();
  } catch (...) {
    return false;
  }
}

std::string generic_relative(const std::filesystem::path& p,
                             const std::filesystem::path& base) {
  // generic_string() guarantees forward slashes on every platform.
  return p.lexically_relative(base).generic_string();
}

}  // namespace

CsvShape probe_csv(const std::filesystem::path& csv) {
  std::ifstream is(csv, std::ios::binary);
  if (!is) {
    throw std::runtime_error("cannot open CSV: " + csv.string());
  }

  CsvShape shape;
  std::string line;
  bool first = true;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    ++shape.rows;
    if (first) {
      first = false;
      size_t start = 0;
      while (start <= line.size()) {
        const size_t pos = line.find(',', start);
        std::string field = line.substr(
            start, pos == std::string::npos ? std::string::npos : pos - start);
        // Strip surrounding quotes before the numeric test.
        if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
          field = field.substr(1, field.size() - 2);
          shape.header_guess = shape.header_guess || !looks_numeric(field);
        } else if (!looks_numeric(field)) {
          shape.header_guess = true;
        }
        ++shape.cols_first_row;
        if (pos == std::string::npos) break;
        start = pos + 1;
      }
    }
  }
  return shape;
}

std::vector<ManifestEntry> scan_dataset(const std::filesystem::path& root) {
  if (!std::filesystem::is_directory(root)) {
    throw std::runtime_error("dataset root is not a directory: " +
                             root.string());
  }

  std::vector<ManifestEntry> entries;
  for (auto it = std::filesystem::recursive_directory_iterator(root);
       it != std::filesystem::recursive_directory_iterator(); ++it) {
    if (it->is_directory()) {
      if (is_hidden(it->path())) it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file() || is_hidden(it->path())) continue;

    ManifestEntry e;
    e.relative_path = generic_relative(it->path(), root);
    e.directory = generic_relative(it->path().parent_path(), root);
    if (e.directory == ".") e.directory.clear();
    const std::string ext = it->path().extension().string();
    e.extension = ext.empty() ? "" : to_lower(ext.substr(1));
    e.size_bytes = it->file_size();
    e.filename_meta = parse_capture_filename(it->path().filename().string());
    if (e.extension == "csv") {
      e.csv_shape = probe_csv(it->path());
    }
    entries.push_back(std::move(e));
  }

  std::sort(entries.begin(), entries.end(),
            [](const ManifestEntry& a, const ManifestEntry& b) {
              return a.relative_path < b.relative_path;
            });
  return entries;
}

std::vector<ExposureSeries> find_exposure_series(
    const std::vector<ManifestEntry>& entries, std::size_t min_distinct) {
  struct Bucket {
    ExposureSeries series;
    std::set<double> shutters;
  };
  // Key: directory | group | aperture | iso, with sentinels for absent fields.
  std::map<std::string, Bucket> buckets;

  for (const auto& e : entries) {
    if (e.extension != "raf") continue;
    const auto& m = e.filename_meta;
    if (!m.shutter_s) continue;

    std::string key = e.directory;
    key += '\x1f';
    key += m.group.value_or("");
    key += '\x1f';
    key += m.aperture ? std::to_string(*m.aperture) : "-";
    key += '\x1f';
    key += m.iso ? std::to_string(*m.iso) : "-";

    auto& b = buckets[key];
    if (b.series.paths.empty()) {
      b.series.directory = e.directory;
      b.series.group = m.group.value_or("");
      b.series.aperture = m.aperture;
      b.series.iso = m.iso;
    }
    b.series.paths.push_back(e.relative_path);
    b.shutters.insert(*m.shutter_s);
  }

  std::vector<ExposureSeries> out;
  for (auto& [key, b] : buckets) {
    if (b.shutters.size() < min_distinct) continue;
    b.series.distinct_shutters = b.shutters.size();
    std::sort(b.series.paths.begin(), b.series.paths.end());
    out.push_back(std::move(b.series));
  }
  std::sort(out.begin(), out.end(),
            [](const ExposureSeries& a, const ExposureSeries& b) {
              if (a.directory != b.directory) return a.directory < b.directory;
              if (a.group != b.group) return a.group < b.group;
              return a.aperture.value_or(0) < b.aperture.value_or(0);
            });
  return out;
}

namespace {

// RAW extensions the toolkit attempts to read with LibRaw.
bool is_raw_extension(std::string_view ext) {
  return ext == "raf" || ext == "nef" || ext == "arw" || ext == "cr2" ||
         ext == "iiq" || ext == "dng";
}

void write_optional(JsonWriter& w, const std::optional<double>& v) {
  if (v) w.value(*v); else w.null();
}

void write_optional(JsonWriter& w, const std::optional<int>& v) {
  if (v) w.value(*v); else w.null();
}

void write_optional(JsonWriter& w, const std::optional<std::string>& v) {
  if (v) w.value(*v); else w.null();
}

}  // namespace

std::size_t populate_raw_metadata(std::vector<ManifestEntry>& entries,
                                  const std::filesystem::path& root) {
  std::size_t populated = 0;
  for (auto& e : entries) {
    if (!is_raw_extension(e.extension)) continue;
    e.raw_meta = read_raw_metadata(root / e.relative_path);
    if (e.raw_meta) ++populated;
  }
  return populated;
}

void write_manifest_json(std::ostream& os, std::string_view root_label,
                         const std::vector<ManifestEntry>& entries,
                         const std::vector<ExposureSeries>& series) {
  JsonWriter w(os);
  w.begin_object();
  w.key("tool");
  w.value("camera_iq");
  w.key("root");
  w.value(root_label);
  w.key("file_count");
  w.value(static_cast<std::int64_t>(entries.size()));

  w.key("files");
  w.begin_array();
  for (const auto& e : entries) {
    w.begin_object();
    w.key("path");
    w.value(e.relative_path);
    w.key("directory");
    w.value(e.directory);
    w.key("extension");
    w.value(e.extension);
    w.key("size_bytes");
    w.value(static_cast<std::int64_t>(e.size_bytes));

    const auto& fm = e.filename_meta;
    const bool has_filename_meta = fm.group || fm.aperture || fm.shutter_s ||
                                   fm.iso || fm.frame;
    w.key("filename_meta");
    if (has_filename_meta) {
      w.begin_object();
      w.key("group");
      write_optional(w, fm.group);
      w.key("aperture");
      write_optional(w, fm.aperture);
      w.key("shutter_s");
      write_optional(w, fm.shutter_s);
      w.key("shutter_str");
      write_optional(w, fm.shutter_str);
      w.key("iso");
      write_optional(w, fm.iso);
      w.key("frame");
      write_optional(w, fm.frame);
      w.end_object();
    } else {
      w.null();
    }

    w.key("csv");
    if (e.csv_shape) {
      w.begin_object();
      w.key("rows");
      w.value(static_cast<std::int64_t>(e.csv_shape->rows));
      w.key("cols_first_row");
      w.value(static_cast<std::int64_t>(e.csv_shape->cols_first_row));
      w.key("header_guess");
      w.value(e.csv_shape->header_guess);
      w.end_object();
    } else {
      w.null();
    }

    w.key("exif");
    if (e.raw_meta) {
      const auto& r = *e.raw_meta;
      w.begin_object();
      w.key("make");
      w.value(r.make);
      w.key("model");
      w.value(r.model);
      w.key("iso");
      w.value(r.iso);
      w.key("shutter_s");
      w.value(r.shutter_s);
      w.key("aperture");
      w.value(r.aperture);
      w.key("focal_length_mm");
      w.value(r.focal_length_mm);
      w.key("timestamp");
      if (r.timestamp.empty()) w.null(); else w.value(r.timestamp);
      w.key("cfa_pattern");
      w.value(r.cfa_pattern);
      w.key("raw_width");
      w.value(r.raw_width);
      w.key("raw_height");
      w.value(r.raw_height);
      w.key("black_level");
      w.value(r.black_level);
      w.key("black_per_channel");
      w.begin_array();
      for (double b : r.black_per_channel) w.value(b);
      w.end_array();
      w.key("white_level");
      w.value(r.white_level);
      w.end_object();
    } else {
      w.null();
    }

    w.end_object();
  }
  w.end_array();

  w.key("exposure_series");
  w.begin_array();
  for (const auto& s : series) {
    w.begin_object();
    w.key("directory");
    w.value(s.directory);
    w.key("group");
    w.value(s.group);
    w.key("aperture");
    write_optional(w, s.aperture);
    w.key("iso");
    write_optional(w, s.iso);
    w.key("distinct_shutters");
    w.value(static_cast<std::int64_t>(s.distinct_shutters));
    w.key("frame_count");
    w.value(static_cast<std::int64_t>(s.paths.size()));
    w.key("paths");
    w.begin_array();
    for (const auto& p : s.paths) w.value(p);
    w.end_array();
    w.end_object();
  }
  w.end_array();

  // Summary counts, keys sorted for deterministic output.
  std::map<std::string, std::int64_t> ext_counts;
  std::map<std::string, std::int64_t> dir_counts;
  for (const auto& e : entries) {
    ++ext_counts[e.extension.empty() ? "(none)" : e.extension];
    ++dir_counts[e.directory.empty() ? "(root)" : e.directory];
  }
  w.key("summary");
  w.begin_object();
  w.key("extension_counts");
  w.begin_object();
  for (const auto& [ext, n] : ext_counts) {
    w.key(ext);
    w.value(n);
  }
  w.end_object();
  w.key("directory_counts");
  w.begin_object();
  for (const auto& [dir, n] : dir_counts) {
    w.key(dir);
    w.value(n);
  }
  w.end_object();
  w.end_object();

  w.end_object();
}

}  // namespace camera_iq
