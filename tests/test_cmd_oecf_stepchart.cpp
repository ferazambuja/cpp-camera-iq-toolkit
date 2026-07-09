#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;

using test::check;

namespace {

void write_file(const fs::path& path, const std::string& text = "") {
  fs::create_directories(path.parent_path());
  std::ofstream os(path, std::ios::binary);
  os << text;
}

int run_stepchart(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return camera_iq::cmd_oecf_stepchart(static_cast<int>(argv.size()),
                                       argv.data());
}

// Runs the command with std::cerr captured so tests can assert the
// gate-naming failure messages the plan requires.
std::pair<int, std::string> run_capture(const std::vector<std::string>& args) {
  std::ostringstream captured;
  std::streambuf* old = std::cerr.rdbuf(captured.rdbuf());
  const int code = run_stepchart(args);
  std::cerr.rdbuf(old);
  return {code, captured.str()};
}

std::string summary_csv(int iso, const std::string& shutter_token, int frame,
                        const std::string& run_date, int zones = 20) {
  std::string text;
  const std::string prefix = "NIKON D800_i" + std::to_string(iso) + "_" +
                             shutter_token + "_";
  text += "Imatest,4.5.7, , Stepchart\n";
  text += "Title,10 file avg (" + prefix + std::to_string(frame) +
          ".NEF & 9 more)\n";
  if (!run_date.empty()) {
    text += "Run date," + run_date + ",,Build 2016-11-22\n";
  }
  text += "File name," + prefix + std::to_string(frame) + ".NEF\n";
  text += "\n";
  text += "10 files combined for analysis.\n";
  for (int i = 0; i < 10; ++i) {
    const int f = (iso == 100 && i == 8) ? 91 : (frame + i);
    text += "File " + std::to_string(i + 1) + ", " + prefix +
            std::to_string(f) + ".NEF\n";
  }
  text += "\n";
  text += "Zones," + std::to_string(zones) + "\n";
  text += "\n";
  text += "Pixel offset,0\n";
  text += "\n";
  text += "OECF primary table follows\n";
  text +=
      "Zone,Pixel,Pixel/255,Log(exp),Log(px/255),Width px,Height px,"
      "Pixels total,Lux (patch),\n";
  for (int z = 1; z <= zones; ++z) {
    double pixel = 42.0 + static_cast<double>(iso) / 10000.0 -
                   static_cast<double>(z - 1) * 2.0;
    if (z >= 19) pixel = 0.2;
    const int width = z == zones ? 200 : 201;
    text += (z < 10 ? " " : "") + std::to_string(z) + ", " +
            std::to_string(pixel) + ", " + std::to_string(pixel / 255.0) +
            ", " + (z == 1 ? "-0.0000" : std::to_string(-0.15 * (z - 1))) +
            ", -1.0000," + std::to_string(width) + ",50," +
            std::to_string(width * 50) + "\n";
  }
  text += "\n";
  text += "Zone,(-)Log(exp),Y-density\n";
  for (int z = 1; z <= 20; ++z) {
    text += std::to_string(z) + ",0.15,0.5,0,0,0\n";
  }
  text += "\n";
  text += "1,Low,6.19\n";
  text += "0.5,Medium,4.77\n";
  text += "Frequency (C/P),Noise power\n  0.005,  0.069\n";
  text += "Zone,Mean noise,S/N\n1,,Inf\n";
  text +=
      "Directory                       , /""Users/private/absolute/D800_OECF\n";
  text += "Directory Number                , 000\n";
  return text;
}

void write_dataset_config(const fs::path& config, const fs::path& dataset_root) {
  write_file(config,
             "{\n"
             "  \"datasets\": {\n"
             "    \"d800_oecf_fixture\": {\n"
             "      \"root\": \"" +
                 dataset_root.string() +
                 "\",\n"
                 "      \"description\": \"fixture\"\n"
                 "    }\n"
                 "  }\n"
                 "}\n");
}

std::vector<std::tuple<int, std::string, int, std::string>> groups() {
  return {{100, "s1-40", 2, "11-Dec-2016 03:19:31"},
          {200, "s1-80", 11, "11-Dec-2016 03:23:20"},
          {400, "s1-160", 21, "11-Dec-2016 03:28:14"},
          {800, "s1-320", 31, "11-Dec-2016 03:31:20"},
          {1600, "s1-640", 41, "11-Dec-2016 03:34:10"},
          {3200, "s1-1250", 51, "11-Dec-2016 03:36:04"},
          {6400, "s1-2500", 61, "11-Dec-2016 03:37:47"},
          {12800, "s1-5000", 71, "11-Dec-2016 03:39:54"}};
}

struct DatasetOpts {
  bool missing_nef = false;
  bool duplicate_summary = false;
  bool bad_date = false;      // unparseable token
  bool mixed_day = false;     // one summary on a different calendar day
  bool bad_zones = false;     // one 19-zone summary
  bool over_window = false;   // same-day span of 3600 s
  bool boundary_window = false;  // same-day span of exactly 1800 s
  bool missing_date = false;  // one summary with no Run date line
  bool impossible_date = false;  // 31-Feb must not renormalize
  bool lenient_date = false;  // single-digit day/hour must be rejected
  bool skip_last_group = false;  // only 7 of 8 summaries
};

void write_dataset(const fs::path& root, const DatasetOpts& opts = {}) {
  fs::create_directories(root / "Results");
  for (const auto& [iso, shutter, frame, date] : groups()) {
    std::string run_date = date;
    if (opts.bad_date && iso == 100) run_date = "not-a-date";
    if (opts.mixed_day && iso == 12800) run_date = "12-Dec-2016 03:39:54";
    if (opts.over_window && iso == 12800) run_date = "11-Dec-2016 04:19:31";
    if (opts.boundary_window && iso == 12800)
      run_date = "11-Dec-2016 03:49:31";
    if (opts.missing_date && iso == 100) run_date = "";
    if (opts.impossible_date && iso == 100)
      run_date = "31-Feb-2016 03:19:31";
    if (opts.lenient_date && iso == 100) run_date = "1-Dec-2016 3:19:31";
    const int zones = opts.bad_zones && iso == 100 ? 19 : 20;
    if (!(opts.skip_last_group && iso == 12800)) {
      write_file(root / "Results" /
                     ("NIKON D800_i" + std::to_string(iso) + "_" + shutter +
                      "_" + std::to_string(frame) + "_comb_10_summary.csv"),
                 summary_csv(iso, shutter, frame, run_date, zones));
    }
    for (int i = 0; i < 10; ++i) {
      const int f = (iso == 100 && i == 8) ? 91 : (frame + i);
      if (opts.missing_nef && iso == 100 && i == 0) continue;
      write_file(root / ("NIKON D800_i" + std::to_string(iso) + "_" +
                         shutter + "_" + std::to_string(f) + ".NEF"));
    }
  }
  if (opts.duplicate_summary) {
    write_file(root / "Results" / "duplicate_i100_summary.csv",
               summary_csv(100, "s1-40", 2, "11-Dec-2016 03:20:00"));
  }
  for (int f = 78; f <= 88; ++f) {
    write_file(root / ("NIKON D800_i25600_s1-5000_" + std::to_string(f) +
                       ".NEF"));
  }
  write_file(root / "2016_12_09_OECF_D800_test_0148.NEF");
  write_file(root / "2016_12_09_OECF_D800_test_0149.NEF");
  write_file(root / "2016_12_09_OECF_D800_test_0150.NEF");
}

std::string read_file(const fs::path& path) {
  std::ifstream is(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(is),
                     std::istreambuf_iterator<char>());
}

std::size_t count_occurrences(const std::string& text,
                              const std::string& needle) {
  std::size_t count = 0;
  for (auto pos = text.find(needle); pos != std::string::npos;
       pos = text.find(needle, pos + needle.size())) {
    ++count;
  }
  return count;
}

// Writes a fresh fixture dataset + config with the given options and runs the
// command with stderr captured.
std::pair<int, std::string> run_variant(const fs::path& root,
                                        const std::string& name,
                                        const DatasetOpts& opts) {
  const fs::path dataset = root / name;
  const fs::path config = root / (name + "_config.json");
  write_dataset(dataset, opts);
  write_dataset_config(config, dataset);
  return run_capture({"d800_oecf_fixture", "--config", config.string(),
                      "--oracle-dir", "Results"});
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_stepchart_cmd";
  fs::remove_all(root);
  fs::create_directories(root);
  const fs::path dataset = root / "dataset";
  const fs::path config = root / "datasets.local.json";
  write_dataset(dataset);
  write_dataset_config(config, dataset);

  check(run_stepchart({}) == 2,
        "oecf-stepchart cmd: dataset argument required");
  check(run_stepchart({"d800_oecf_fixture", "--config", config.string()}) == 2,
        "oecf-stepchart cmd: oracle dir required");
  check(run_stepchart({"--help"}) == 0,
        "oecf-stepchart cmd: --help prints usage and exits 0");
  check(run_stepchart({"d800_oecf_fixture", "--config", config.string(),
                       "--oracle-dir", "../outside"}) == 2,
        "oecf-stepchart cmd: --oracle-dir traversal rejected as arg error");

  // Nested --out parent directories must be created (house cmd_sfr pattern).
  const fs::path out = root / "nested" / "deep" / "out.json";
  const int ok = run_stepchart({"d800_oecf_fixture", "--config",
                                config.string(), "--oracle-dir", "Results",
                                "--out", out.string()});
  check(ok == 0, "oecf-stepchart cmd: fixture archive succeeds");
  check(fs::exists(out),
        "oecf-stepchart cmd: creates nested --out parent directories");
  const std::string json = read_file(out);
  check(json.find("\"command\":\"oecf-stepchart\"") != std::string::npos,
        "oecf-stepchart cmd: emits command");
  check(json.find("\"mode\":\"oecf_stepchart_oracle\"") != std::string::npos,
        "oecf-stepchart cmd: emits mode");
  check(json.find("\"dataset\":\"dataset:d800_oecf_fixture\"") !=
            std::string::npos,
        "oecf-stepchart cmd: emits sanitized dataset label");
  check(json.find("\"oracle_summary_count\":8") != std::string::npos,
        "oecf-stepchart cmd: emits 8 summaries");
  check(json.find("\"span_seconds\":1223") != std::string::npos,
        "oecf-stepchart cmd: emits pinned run-date span");
  check(json.find("\"iso\":25600") != std::string::npos,
        "oecf-stepchart cmd: emits ISO25600 diagnostic group");
  check(json.find("diagnostic_iso25600_unoracled_one_stop_over_compensated") !=
            std::string::npos,
        "oecf-stepchart cmd: ISO25600 group carries the diagnostic reason");
  check(json.find("2016_12_09_OECF_D800_test_0148.NEF") !=
            std::string::npos,
        "oecf-stepchart cmd: emits test-file orphan group");
  check(json.find("\"advisory_cross_iso_pixel_spread_by_zone\":[") !=
            std::string::npos,
        "oecf-stepchart cmd: emits cross-ISO advisory array");
  check(json.find("\"zone\":1,\"min\":42.01,\"max\":43.28") !=
            std::string::npos,
        "oecf-stepchart cmd: advisory pins bright-zone envelope");
  check(json.find("\"zone\":20,\"min\":0.2,\"max\":0.2,\"spread\":0") !=
            std::string::npos,
        "oecf-stepchart cmd: advisory pins all 20 zones through shadow end");
  {
    const auto adv_begin =
        json.find("\"advisory_cross_iso_pixel_spread_by_zone\":[");
    const auto adv_end = json.find("]", adv_begin);
    const std::string advisory = json.substr(adv_begin, adv_end - adv_begin);
    check(count_occurrences(advisory, "\"zone\":") == 20,
          "oecf-stepchart cmd: advisory has exactly 20 zone entries");
  }
  check(json.find("\"iso_14524_oecf_conformance\"") != std::string::npos &&
            json.find("\"raw_dn_oecf\"") != std::string::npos &&
            json.find("\"raw_stepchart_zone_extraction\"") !=
                std::string::npos &&
            json.find("\"ptc_or_dynamic_range\"") != std::string::npos &&
            json.find("\"chart_density_traceability\"") != std::string::npos &&
            json.find("\"measured_iso_speed\"") != std::string::npos,
        "oecf-stepchart cmd: emits all six not-claimed caveats");
  check(json.find("/""Users/") == std::string::npos,
        "oecf-stepchart cmd: strips private Directory paths from JSON");

  {
    DatasetOpts opts;
    opts.missing_nef = true;
    const auto [code, err] = run_variant(root, "missing", opts);
    check(code == 1, "oecf-stepchart cmd: missing listed NEF is runtime failure");
    check(err.find("listed NEF missing") != std::string::npos,
          "oecf-stepchart cmd: missing-NEF message names the join gate");
  }

  {
    DatasetOpts opts;
    opts.duplicate_summary = true;
    const auto [code, err] = run_variant(root, "duplicate", opts);
    check(code == 1, "oecf-stepchart cmd: duplicate ISO/shutter summary rejected");
    check(err.find("duplicate summary ISO/shutter") != std::string::npos,
          "oecf-stepchart cmd: duplicate message names the gate");
  }

  {
    DatasetOpts opts;
    opts.bad_date = true;
    const auto [code, err] = run_variant(root, "bad_date", opts);
    check(code == 1, "oecf-stepchart cmd: unparseable run date rejected");
    check(err.find("unparseable run date") != std::string::npos,
          "oecf-stepchart cmd: unparseable-date message names the gate");
  }

  {
    DatasetOpts opts;
    opts.missing_date = true;
    const auto [code, err] = run_variant(root, "missing_date", opts);
    check(code == 1,
          "oecf-stepchart cmd: summary without Run date line rejected");
    check(err.find("missing run date") != std::string::npos,
          "oecf-stepchart cmd: missing-date failure is parser hard-fail");
  }

  {
    DatasetOpts opts;
    opts.impossible_date = true;
    const auto [code, err] = run_variant(root, "impossible_date", opts);
    check(code == 1,
          "oecf-stepchart cmd: impossible 31-Feb date rejected, not renormalized");
    check(err.find("unparseable run date") != std::string::npos,
          "oecf-stepchart cmd: impossible-date message names the gate");
  }

  {
    DatasetOpts opts;
    opts.lenient_date = true;
    const auto [code, err] = run_variant(root, "lenient_date", opts);
    check(code == 1,
          "oecf-stepchart cmd: single-digit day/hour date rejected (strict shape)");
    check(err.find("unparseable run date") != std::string::npos,
          "oecf-stepchart cmd: lenient-date message names the gate");
  }

  {
    DatasetOpts opts;
    opts.mixed_day = true;
    const auto [code, err] = run_variant(root, "mixed_day", opts);
    check(code == 1, "oecf-stepchart cmd: mixed-day run window rejected");
    check(err.find("multiple calendar days") != std::string::npos,
          "oecf-stepchart cmd: mixed-day message names the window gate");
  }

  {
    DatasetOpts opts;
    opts.over_window = true;
    const auto [code, err] = run_variant(root, "over_window", opts);
    check(code == 1,
          "oecf-stepchart cmd: same-day over-30-minute window rejected");
    check(err.find("exceeds 30 minutes") != std::string::npos,
          "oecf-stepchart cmd: over-window message names the window gate");
  }

  {
    DatasetOpts opts;
    opts.boundary_window = true;
    const auto [code, err] = run_variant(root, "boundary_window", opts);
    check(code == 0 && err.empty(),
          "oecf-stepchart cmd: exactly-30-minute window accepted");
  }

  {
    DatasetOpts opts;
    opts.bad_zones = true;
    const auto [code, err] = run_variant(root, "bad_zones", opts);
    check(code == 1, "oecf-stepchart cmd: archive-level zone-count gate rejected");
    check(err.find("expected 20 zones") != std::string::npos,
          "oecf-stepchart cmd: zone-count message names the violated gate");
  }

  {
    DatasetOpts opts;
    opts.skip_last_group = true;
    const auto [code, err] = run_variant(root, "seven_of_eight", opts);
    check(code == 1, "oecf-stepchart cmd: 7-of-8 ISO groups rejected");
    check(err.find("expected 8 summaries") != std::string::npos,
          "oecf-stepchart cmd: summary-count message names the violated gate");
  }

  fs::remove_all(root);
}
