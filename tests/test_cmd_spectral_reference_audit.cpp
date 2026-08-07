#include "camera_iq/commands.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;
using test::check;

namespace {

int run_audit(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return camera_iq::cmd_spectral_reference_audit(
      static_cast<int>(argv.size()), argv.data());
}

std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void write_file(const fs::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary);
  output << text;
}

void replace_first(std::string& text, const std::string& old_text,
                   const std::string& new_text) {
  const auto position = text.find(old_text);
  if (position == std::string::npos) {
    throw std::runtime_error("spectral audit command fixture text not found");
  }
  text.replace(position, old_text.size(), new_text);
}

void set_option(std::vector<std::string>& args, const std::string& option,
                const fs::path& value) {
  const auto found = std::find(args.begin(), args.end(), option);
  if (found == args.end() || std::next(found) == args.end()) {
    throw std::runtime_error("spectral audit command option not found");
  }
  *std::next(found) = value.string();
}

}  // namespace

void TESTS() {
  const fs::path source = CAMERA_IQ_SOURCE_DIR;
  const fs::path samples = source / "data/samples/spectral_2017";
  const fs::path root =
      fs::temp_directory_path() / "camera_iq_spectral_reference_audit_cmd";
  fs::remove_all(root);
  fs::create_directories(root);
  const fs::path json = root / "audit.json";
  const fs::path csv = root / "repeat.csv";
  const std::vector<std::string> args = {
      "--spectrashop", (samples / "CC4_CGATS.txt").string(),
      "--alternate-spectrashop", (samples / "CC4_4.txt").string(),
      "--babelcolor", (samples / "CC4_CGATS_M0.txt").string(),
      "--layout-export", (samples / "CC4_4_M0.txt").string(),
      "--repeat-first", (samples / "colorchecker_measurement_01.csv").string(),
      "--repeat-second", (samples / "colorchecker_measurement_02.csv").string(),
      "--d65", (source / "data/cie_d65.csv").string(),
      "--observer-10", (source / "data/cie1964_10deg_cmf.csv").string(),
      "--observer-2", (source / "data/cie1931_2deg_cmf.csv").string(),
      "--d55", (source / "data/cie_d55.csv").string(),
      "--out-json", json.string(), "--out-csv", csv.string()};
  check(run_audit(args) == 0,
        "spectral reference audit cmd: public evidence run succeeds");
  const std::string result = read_file(json);
  check(result.find("\"mean_delta_e_76\":0.011857") != std::string::npos &&
            result.find("\"observer_metadata_conflict\":true") !=
                std::string::npos &&
            result.find("\"cgats_evidence_scope\":\"one_measurement_reserialized\"") !=
                std::string::npos &&
            result.find("\"export_id\":\"spectrashop_primary\","
                        "\"declared_field_count\":38,\"actual_field_count\":41,"
                        "\"field_count_matches\":false") !=
                std::string::npos &&
            result.find("\"export_id\":\"babelcolor_xyz\","
                        "\"declared_field_count\":41,\"actual_field_count\":41,"
                        "\"field_count_matches\":true") !=
                std::string::npos,
        "spectral reference audit cmd: observer, scope, and per-export schema are explicit");
  const std::string repeat_csv = read_file(csv);
  check(repeat_csv.find("patch_id,reflectance_rms,delta_e_76") == 0,
        "spectral reference audit cmd: repeat detail is inspectable");
  check(repeat_csv.find("patch_13,0.003885259590,1.082758181971") !=
            std::string::npos,
        "spectral reference audit cmd: public numeric rows use stable precision");

  const fs::path mismatched_export = root / "mismatched.txt";
  std::string changed_export = read_file(samples / "CC4_4.txt");
  replace_first(changed_export, "0.057613", "0.057614");
  write_file(mismatched_export, changed_export);
  auto mismatched = args;
  const fs::path mismatched_json = root / "mismatched.json";
  const fs::path mismatched_csv = root / "mismatched.csv";
  set_option(mismatched, "--alternate-spectrashop", mismatched_export);
  set_option(mismatched, "--out-json", mismatched_json);
  set_option(mismatched, "--out-csv", mismatched_csv);
  check(run_audit(mismatched) == 1 && !fs::exists(mismatched_json) &&
            !fs::exists(mismatched_csv),
        "spectral reference audit cmd: non-identical exports cannot receive "
        "the reserialization scope");

  const fs::path quoted_first = root / "quoted-first.csv";
  const fs::path quoted_second = root / "quoted-second.csv";
  std::string first_text = read_file(samples / "colorchecker_measurement_01.csv");
  std::string second_text = read_file(samples / "colorchecker_measurement_02.csv");
  replace_first(first_text, "patch_01,", "\"patch,\"\"one\"\"\",");
  replace_first(second_text, "patch_01,", "\"patch,\"\"one\"\"\",");
  write_file(quoted_first, first_text);
  write_file(quoted_second, second_text);
  auto quoted = args;
  const fs::path quoted_json = root / "quoted.json";
  const fs::path quoted_csv = root / "quoted.csv";
  set_option(quoted, "--repeat-first", quoted_first);
  set_option(quoted, "--repeat-second", quoted_second);
  set_option(quoted, "--out-json", quoted_json);
  set_option(quoted, "--out-csv", quoted_csv);
  check(run_audit(quoted) == 0 &&
            read_file(quoted_csv).find("\n\"patch,\"\"one\"\"\",") !=
                std::string::npos,
        "spectral reference audit cmd: legal quoted patch IDs remain valid CSV");

  auto invalid = args;
  invalid[1] = (root / "missing.txt").string();
  const fs::path absent = root / "absent.json";
  invalid[invalid.size() - 3] = absent.string();
  check(run_audit(invalid) == 1 && !fs::exists(absent),
        "spectral reference audit cmd: input failure precedes output creation");
  check(run_audit({"--help"}) == 0,
        "spectral reference audit cmd: help succeeds");
  fs::remove_all(root);
}
