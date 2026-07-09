#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
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

std::string summary_csv(int iso, const std::string& shutter_token, int frame,
                        const std::string& run_date, int zones = 20) {
  std::string text;
  const std::string prefix = "NIKON D800_i" + std::to_string(iso) + "_" +
                             shutter_token + "_";
  text += "Imatest,4.5.7, , Stepchart\n";
  text += "Run date," + run_date + ",,Build 2016-11-22\n";
  text += "File name," + prefix + std::to_string(frame) + ".NEF\n";
  text += "10 files combined for analysis.\n";
  for (int i = 0; i < 10; ++i) {
    const int f = (iso == 100 && i == 8) ? 91 : (frame + i);
    text += "File " + std::to_string(i + 1) + ", " + prefix +
            std::to_string(f) + ".NEF\n";
  }
  text += "Zones," + std::to_string(zones) + "\n";
  text += "Pixel offset,0\n";
  text += "OECF primary table follows\n";
  text +=
      "Zone,Pixel,Pixel/255,Log(exp),Log(px/255),Width px,Height px,"
      "Pixels total,Lux (patch),\n";
  for (int z = 1; z <= zones; ++z) {
    double pixel = 42.0 + static_cast<double>(iso) / 10000.0 -
                   static_cast<double>(z - 1) * 2.0;
    if (z >= 19) pixel = 0.2;
    const int width = z == zones ? 200 : 201;
    text += (z < 10 ? " " : "") + std::to_string(z) + "," +
            std::to_string(pixel) + "," + std::to_string(pixel / 255.0) +
            "," + (z == 1 ? "-0.0000" : std::to_string(-0.15 * (z - 1))) +
            ",-1.0000," + std::to_string(width) + ",50," +
            std::to_string(width * 50) + "\n";
  }
  text += "\n";
  text += "Zone,(-)Log(exp),Y-density\n1,0,0,0,0,0\n";
  text += "Frequency (C/P),Noise power\n1,999\n";
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

void write_dataset(const fs::path& root, bool missing_nef = false,
                   bool duplicate_summary = false,
                   bool bad_date = false, bool mixed_day = false,
                   bool bad_zones = false) {
  fs::create_directories(root / "Results");
  for (const auto& [iso, shutter, frame, date] : groups()) {
    std::string run_date = date;
    if (bad_date && iso == 100) run_date = "not-a-date";
    if (mixed_day && iso == 12800) run_date = "12-Dec-2016 03:39:54";
    const int zones = bad_zones && iso == 100 ? 19 : 20;
    write_file(root / "Results" /
                   ("NIKON D800_i" + std::to_string(iso) + "_" + shutter +
                    "_" + std::to_string(frame) + "_comb_10_summary.csv"),
               summary_csv(iso, shutter, frame, run_date, zones));
    for (int i = 0; i < 10; ++i) {
      const int f = (iso == 100 && i == 8) ? 91 : (frame + i);
      if (missing_nef && iso == 100 && i == 0) continue;
      write_file(root / ("NIKON D800_i" + std::to_string(iso) + "_" +
                         shutter + "_" + std::to_string(f) + ".NEF"));
    }
  }
  if (duplicate_summary) {
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

  const fs::path out = root / "out.json";
  const int ok = run_stepchart({"d800_oecf_fixture", "--config",
                                config.string(), "--oracle-dir", "Results",
                                "--out", out.string()});
  check(ok == 0, "oecf-stepchart cmd: fixture archive succeeds");
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
  check(json.find("\"chart_density_traceability\"") != std::string::npos &&
            json.find("\"measured_iso_speed\"") != std::string::npos,
        "oecf-stepchart cmd: emits required not-claimed caveats");
  check(json.find("/""Users/") == std::string::npos,
        "oecf-stepchart cmd: strips private Directory paths from JSON");

  const fs::path missing = root / "missing";
  const fs::path missing_config = root / "missing_config.json";
  write_dataset(missing, true);
  write_dataset_config(missing_config, missing);
  check(run_stepchart({"d800_oecf_fixture", "--config", missing_config.string(),
                       "--oracle-dir", "Results"}) == 1,
        "oecf-stepchart cmd: missing listed NEF is runtime failure");

  const fs::path dup = root / "duplicate";
  const fs::path dup_config = root / "dup_config.json";
  write_dataset(dup, false, true);
  write_dataset_config(dup_config, dup);
  check(run_stepchart({"d800_oecf_fixture", "--config", dup_config.string(),
                       "--oracle-dir", "Results"}) == 1,
        "oecf-stepchart cmd: duplicate ISO/shutter summary rejected");

  const fs::path bad_date_root = root / "bad_date";
  const fs::path bad_date_config = root / "bad_date_config.json";
  write_dataset(bad_date_root, false, false, true);
  write_dataset_config(bad_date_config, bad_date_root);
  check(run_stepchart({"d800_oecf_fixture", "--config",
                       bad_date_config.string(), "--oracle-dir",
                       "Results"}) == 1,
        "oecf-stepchart cmd: unparseable run date rejected");

  const fs::path mixed_root = root / "mixed_day";
  const fs::path mixed_config = root / "mixed_config.json";
  write_dataset(mixed_root, false, false, false, true);
  write_dataset_config(mixed_config, mixed_root);
  check(run_stepchart({"d800_oecf_fixture", "--config", mixed_config.string(),
                       "--oracle-dir", "Results"}) == 1,
        "oecf-stepchart cmd: mixed-day run window rejected");

  const fs::path bad_zones_root = root / "bad_zones";
  const fs::path bad_zones_config = root / "bad_zones_config.json";
  write_dataset(bad_zones_root, false, false, false, false, true);
  write_dataset_config(bad_zones_config, bad_zones_root);
  check(run_stepchart({"d800_oecf_fixture", "--config",
                       bad_zones_config.string(), "--oracle-dir",
                       "Results"}) == 1,
        "oecf-stepchart cmd: archive-level zone-count gate rejected");

  fs::remove_all(root);
}
