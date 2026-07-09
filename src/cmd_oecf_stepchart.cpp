#include "camera_iq/commands.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/dataset_config.hpp"
#include "camera_iq/filename_meta.hpp"
#include "camera_iq/imatest_stepchart.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/manifest.hpp"

namespace camera_iq {
namespace {

struct Args {
  std::string root_or_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path oracle_dir;
  std::filesystem::path out;
};

struct ParsedRunDate {
  int year = 0;
  int month = 0;
  int day = 0;
  // Seconds on a timezone-free civil timeline (days-from-civil * 86400 +
  // time-of-day). Window math must be a function of the token text only —
  // routing through mktime would make the gate depend on the machine
  // timezone (a DST spring-forward can shrink a 62-minute wall-clock span
  // to a passing 2 minutes).
  long long civil_seconds = 0;
};

struct SummaryGroup {
  ImatestStepchartSummary summary;
  std::filesystem::path summary_rel;
  int iso = 0;
  std::string shutter_token;
  double shutter_s = 0.0;
};

struct OrphanGroup {
  std::optional<int> iso;
  std::string shutter_token;
  std::string reason;
  std::vector<std::string> files;
};

// Strict parse of Imatest's exact `11-Dec-2016 03:19:31` shape. Impossible
// calendar dates hard-fail here instead of being renormalized the way
// std::mktime silently would (31-Feb -> 02-Mar).
std::optional<ParsedRunDate> parse_run_date(const std::string& text) {
  static const std::regex kShape(
      R"(^(\d{2})-([A-Z][a-z]{2})-(\d{4}) (\d{2}):(\d{2}):(\d{2})$)");
  static const std::map<std::string, int> kMonths{
      {"Jan", 1}, {"Feb", 2}, {"Mar", 3},  {"Apr", 4},  {"May", 5},
      {"Jun", 6}, {"Jul", 7}, {"Aug", 8},  {"Sep", 9},  {"Oct", 10},
      {"Nov", 11}, {"Dec", 12}};
  std::smatch m;
  if (!std::regex_match(text, m, kShape)) return std::nullopt;
  const auto month_it = kMonths.find(m[2].str());
  if (month_it == kMonths.end()) return std::nullopt;
  const int day = std::stoi(m[1].str());
  const int year = std::stoi(m[3].str());
  const int hour = std::stoi(m[4].str());
  const int minute = std::stoi(m[5].str());
  const int second = std::stoi(m[6].str());
  const int month = month_it->second;
  if (day < 1 || hour > 23 || minute > 59 || second > 59) return std::nullopt;
  static constexpr std::array<int, 12> kDaysInMonth{31, 28, 31, 30, 31, 30,
                                                    31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  const int days_in_month =
      (month == 2 && leap) ? 29 : kDaysInMonth[static_cast<std::size_t>(month - 1)];
  if (day > days_in_month) return std::nullopt;
  // Howard Hinnant's days-from-civil: proleptic-Gregorian day count from
  // 1970-01-01, no timezone involvement.
  const int y = year - (month <= 2 ? 1 : 0);
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = static_cast<unsigned>(
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long long days =
      static_cast<long long>(era) * 146097 + static_cast<long long>(doe) -
      719468;
  ParsedRunDate out;
  out.year = year;
  out.month = month;
  out.day = day;
  out.civil_seconds =
      days * 86400LL + hour * 3600LL + minute * 60LL + second;
  return out;
}

bool same_calendar_day(const ParsedRunDate& a, const ParsedRunDate& b) {
  return a.year == b.year && a.month == b.month && a.day == b.day;
}

std::string rel_label(const ResolvedDataset& dataset,
                      const std::filesystem::path& rel) {
  return dataset.from_config ? dataset_file_label(dataset.id, rel)
                             : rel.generic_string();
}

bool has_suffix(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() &&
         text.substr(text.size() - suffix.size()) == suffix;
}

bool is_nef_entry(const ManifestEntry& e) { return e.extension == "nef"; }

std::vector<std::filesystem::path> find_summary_files(
    const std::filesystem::path& dir) {
  if (!std::filesystem::is_directory(dir)) {
    throw std::runtime_error("oracle dir is not a directory: " + dir.string());
  }
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (!entry.is_regular_file()) continue;
    const auto name = entry.path().filename().string();
    if (entry.path().extension() == ".csv" && has_suffix(name, "_summary.csv")) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

std::string shutter_token_from_summary(const ImatestStepchartSummary& summary) {
  const auto meta = parse_capture_filename(summary.file_name);
  if (!meta.shutter_str) {
    throw std::runtime_error("summary filename missing shutter token: " +
                             summary.file_name);
  }
  return *meta.shutter_str;
}

int iso_from_summary(const ImatestStepchartSummary& summary) {
  const auto meta = parse_capture_filename(summary.file_name);
  if (!meta.iso) {
    throw std::runtime_error("summary filename missing ISO token: " +
                             summary.file_name);
  }
  return *meta.iso;
}

double shutter_seconds_from_summary(const ImatestStepchartSummary& summary) {
  const auto meta = parse_capture_filename(summary.file_name);
  if (!meta.shutter_s) {
    throw std::runtime_error("summary filename missing shutter seconds: " +
                             summary.file_name);
  }
  return *meta.shutter_s;
}

void validate_archive_gates(const std::vector<SummaryGroup>& groups) {
  if (groups.size() != 8) {
    throw std::runtime_error("archive-level gate failed: expected 8 summaries");
  }
  const std::set<int> expected_iso{100, 200, 400, 800, 1600,
                                   3200, 6400, 12800};
  std::set<int> seen_iso;
  for (const auto& g : groups) {
    seen_iso.insert(g.iso);
    if (g.summary.declared_zone_count != 20 || g.summary.zones.size() != 20) {
      throw std::runtime_error(
          "archive-level gate failed: expected 20 zones per summary");
    }
    if (g.summary.declared_file_count != 10 ||
        g.summary.combined_files.size() != 10) {
      throw std::runtime_error(
          "archive-level gate failed: expected 10 combined files per summary");
    }
  }
  if (seen_iso != expected_iso) {
    throw std::runtime_error(
        "archive-level gate failed: expected ISO100..12800 summaries");
  }
}

void validate_run_date_window(const std::vector<SummaryGroup>& groups,
                              std::string* first, std::string* last,
                              int* span_seconds) {
  if (groups.empty()) throw std::runtime_error("no summaries for run window");
  std::vector<std::pair<ParsedRunDate, std::string>> dates;
  dates.reserve(groups.size());
  for (const auto& g : groups) {
    const auto parsed = parse_run_date(g.summary.run_date);
    if (!parsed) {
      throw std::runtime_error("unparseable run date: " +
                               g.summary.run_date);
    }
    dates.push_back({*parsed, g.summary.run_date});
  }
  auto [min_it, max_it] = std::minmax_element(
      dates.begin(), dates.end(), [](const auto& a, const auto& b) {
        return a.first.civil_seconds < b.first.civil_seconds;
      });
  if (!same_calendar_day(min_it->first, max_it->first)) {
    throw std::runtime_error("run-date window spans multiple calendar days");
  }
  const int span = static_cast<int>(max_it->first.civil_seconds -
                                    min_it->first.civil_seconds);
  if (span > 1800) {
    throw std::runtime_error("run-date window exceeds 30 minutes");
  }
  *first = min_it->second;
  *last = max_it->second;
  *span_seconds = span;
}

std::vector<OrphanGroup> classify_orphans(
    const std::vector<ManifestEntry>& entries,
    const std::set<std::string>& listed_files) {
  std::map<std::string, OrphanGroup> by_key;
  for (const auto& e : entries) {
    if (!is_nef_entry(e)) continue;
    const std::string name = std::filesystem::path(e.relative_path).filename();
    if (listed_files.count(name) > 0) continue;
    const auto meta = e.filename_meta;
    std::string key;
    OrphanGroup group;
    if (meta.iso && *meta.iso == 25600) {
      group.iso = meta.iso;
      if (meta.shutter_str) group.shutter_token = *meta.shutter_str;
      group.reason = "diagnostic_iso25600_unoracled_one_stop_over_compensated";
      key = "iso25600";
    } else if (meta.iso && meta.shutter_str) {
      // One group per distinct (ISO, shutter) pair so a stray unlisted
      // ISO-tagged NEF cannot mislabel the iso-less test-file group.
      group.iso = meta.iso;
      group.shutter_token = *meta.shutter_str;
      group.reason = "unoracled_test_or_unmatched_raw";
      key = "i" + std::to_string(*meta.iso) + "_" + *meta.shutter_str;
    } else {
      group.reason = "unoracled_test_or_unmatched_raw";
      key = "test";
    }
    auto [it, inserted] = by_key.emplace(key, std::move(group));
    (void)inserted;
    it->second.files.push_back(e.relative_path);
  }
  std::vector<OrphanGroup> out;
  out.reserve(by_key.size());
  for (auto& [_, group] : by_key) {
    std::sort(group.files.begin(), group.files.end());
    out.push_back(std::move(group));
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return a.reason < b.reason;
  });
  return out;
}

void write_string_array(JsonWriter& w, const std::vector<std::string>& values) {
  w.begin_array();
  for (const auto& v : values) w.value(v);
  w.end_array();
}

void write_zone(JsonWriter& w, const ImatestStepchartZone& z) {
  w.begin_object();
  w.key("zone");
  w.value(z.zone);
  w.key("pixel");
  w.value(z.pixel);
  w.key("pixel_255");
  w.value(z.pixel_255);
  w.key("log_exp");
  w.value(z.log_exposure);
  w.key("log_px_255");
  w.value(z.log_pixel_255);
  w.key("width_px");
  w.value(z.width_px);
  w.key("height_px");
  w.value(z.height_px);
  w.key("pixels_total");
  w.value(z.pixels_total);
  w.end_object();
}

void write_json(std::ostream& os, const ResolvedDataset& dataset,
                const std::vector<SummaryGroup>& groups,
                const std::vector<OrphanGroup>& orphans,
                const std::string& first_run_date,
                const std::string& last_run_date, int span_seconds) {
  JsonWriter w(os);
  w.begin_object();
  w.key("command");
  w.value("oecf-stepchart");
  w.key("mode");
  w.value("oecf_stepchart_oracle");
  w.key("dataset");
  w.value(dataset_display_label(dataset));
  w.key("oracle_summary_count");
  w.value(static_cast<int>(groups.size()));
  w.key("run_date_window");
  w.begin_object();
  w.key("first");
  w.value(first_run_date);
  w.key("last");
  w.value(last_run_date);
  w.key("span_seconds");
  w.value(span_seconds);
  w.end_object();

  w.key("summaries");
  w.begin_array();
  for (const auto& g : groups) {
    w.begin_object();
    w.key("summary_file");
    w.value(rel_label(dataset, g.summary_rel));
    w.key("imatest_version");
    w.value(g.summary.imatest_version);
    w.key("run_date");
    w.value(g.summary.run_date);
    w.key("iso");
    w.value(g.iso);
    w.key("shutter_token");
    w.value(g.shutter_token);
    w.key("shutter_s");
    w.value(g.shutter_s);
    w.key("combined_file_count");
    w.value(g.summary.declared_file_count);
    w.key("zone_count");
    w.value(g.summary.declared_zone_count);
    w.key("file_name");
    w.value(g.summary.file_name);
    w.key("combined_files");
    w.begin_array();
    for (const auto& f : g.summary.combined_files) {
      w.value(rel_label(dataset, f));
    }
    w.end_array();
    w.key("zones");
    w.begin_array();
    for (const auto& z : g.summary.zones) write_zone(w, z);
    w.end_array();
    w.end_object();
  }
  w.end_array();

  w.key("orphan_raw_groups");
  w.begin_array();
  for (const auto& group : orphans) {
    w.begin_object();
    w.key("iso");
    if (group.iso) {
      w.value(*group.iso);
    } else {
      w.null();
    }
    w.key("shutter_token");
    w.value(group.shutter_token);
    w.key("reason");
    w.value(group.reason);
    w.key("file_count");
    w.value(static_cast<int>(group.files.size()));
    w.key("files");
    w.begin_array();
    for (const auto& f : group.files) w.value(rel_label(dataset, f));
    w.end_array();
    w.end_object();
  }
  w.end_array();

  w.key("advisory_cross_iso_pixel_spread_by_zone");
  w.begin_array();
  if (!groups.empty()) {
    const std::size_t zone_count = groups.front().summary.zones.size();
    for (std::size_t i = 0; i < zone_count; ++i) {
      double min_v = groups.front().summary.zones[i].pixel;
      double max_v = min_v;
      for (const auto& g : groups) {
        min_v = std::min(min_v, g.summary.zones[i].pixel);
        max_v = std::max(max_v, g.summary.zones[i].pixel);
      }
      w.begin_object();
      w.key("zone");
      w.value(static_cast<int>(i + 1));
      w.key("min");
      w.value(min_v);
      w.key("max");
      w.value(max_v);
      w.key("spread");
      w.value(max_v - min_v);
      w.end_object();
    }
  }
  w.end_array();

  w.key("not_claimed");
  write_string_array(w, {"iso_14524_oecf_conformance", "raw_dn_oecf",
                         "raw_stepchart_zone_extraction",
                         "ptc_or_dynamic_range",
                         "chart_density_traceability",
                         "measured_iso_speed"});
  w.end_object();
}

bool require_value(int argc, int next, std::string_view arg) {
  if (next < argc) return true;
  std::cerr << "camera_iq oecf-stepchart: " << arg << " requires a value\n";
  return false;
}

void usage() {
  std::cout << "Usage: camera_iq oecf-stepchart <dataset-root-or-id>"
               " --oracle-dir REL [--config FILE] [--out FILE]\n";
}

}  // namespace

int cmd_oecf_stepchart(int argc, char** argv) {
  Args args;
  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (arg == "--config") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.config = argv[++i];
    } else if (arg == "--oracle-dir") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.oracle_dir = argv[++i];
    } else if (arg == "--out") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.out = argv[++i];
    } else if (args.root_or_id.empty()) {
      args.root_or_id = std::string(arg);
    } else {
      std::cerr << "camera_iq oecf-stepchart: unexpected argument '" << arg
                << "'\n";
      return 2;
    }
  }

  if (args.root_or_id.empty()) {
    std::cerr << "Usage: camera_iq oecf-stepchart <dataset-root-or-id>"
                 " --oracle-dir REL [--config FILE] [--out FILE]\n";
    return 2;
  }
  if (args.oracle_dir.empty()) {
    std::cerr << "camera_iq oecf-stepchart: --oracle-dir is required\n";
    return 2;
  }
  if (args.oracle_dir.is_absolute()) {
    std::cerr << "camera_iq oecf-stepchart: --oracle-dir must be relative\n";
    return 2;
  }
  for (const auto& part : args.oracle_dir) {
    if (part == "..") {
      std::cerr << "camera_iq oecf-stepchart: --oracle-dir must not contain"
                   " '..' (evidence must stay inside the dataset root)\n";
      return 2;
    }
  }

  try {
    const auto dataset = resolve_dataset_root(args.root_or_id, args.config);
    if (!dataset) {
      std::cerr << "camera_iq oecf-stepchart: '" << args.root_or_id
                << "' is not a directory or dataset id in " << args.config
                << "\n";
      return 1;
    }

    const auto entries = scan_dataset(dataset->root);
    const auto summary_files = find_summary_files(dataset->root / args.oracle_dir);
    std::vector<SummaryGroup> groups;
    groups.reserve(summary_files.size());
    std::set<std::pair<int, std::string>> seen_groups;
    std::set<std::string> listed_basenames;

    for (const auto& path : summary_files) {
      SummaryGroup group;
      group.summary = read_imatest_stepchart_summary(path);
      group.summary_rel = path.lexically_relative(dataset->root);
      group.iso = iso_from_summary(group.summary);
      group.shutter_token = shutter_token_from_summary(group.summary);
      group.shutter_s = shutter_seconds_from_summary(group.summary);
      const auto key = std::make_pair(group.iso, group.shutter_token);
      if (!seen_groups.insert(key).second) {
        std::cerr << "camera_iq oecf-stepchart: duplicate summary ISO/shutter "
                  << group.iso << " " << group.shutter_token << "\n";
        return 1;
      }
      for (const auto& f : group.summary.combined_files) {
        listed_basenames.insert(f);
        // Exact-location join: the capture files live at the dataset root; a
        // basename match anywhere in the tree (e.g. a stray Results/ backup
        // copy) must not satisfy the existence gate.
        if (!std::filesystem::exists(dataset->root / f)) {
          std::cerr << "camera_iq oecf-stepchart: listed NEF missing: " << f
                    << "\n";
          return 1;
        }
      }
      groups.push_back(std::move(group));
    }
    std::sort(groups.begin(), groups.end(),
              [](const SummaryGroup& a, const SummaryGroup& b) {
                return a.iso < b.iso;
              });

    validate_archive_gates(groups);
    std::string first_date;
    std::string last_date;
    int span_seconds = 0;
    validate_run_date_window(groups, &first_date, &last_date, &span_seconds);
    const auto orphans = classify_orphans(entries, listed_basenames);

    if (args.out.empty()) {
      write_json(std::cout, *dataset, groups, orphans, first_date, last_date,
                 span_seconds);
      std::cout << "\n";
    } else {
      if (!args.out.parent_path().empty()) {
        std::filesystem::create_directories(args.out.parent_path());
      }
      std::ofstream os(args.out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq oecf-stepchart: cannot write " << args.out
                  << "\n";
        return 1;
      }
      write_json(os, *dataset, groups, orphans, first_date, last_date,
                 span_seconds);
      os << "\n";
    }
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq oecf-stepchart: " << ex.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
