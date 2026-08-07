#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;
using test::check;

namespace {

void write_file(const fs::path& path, const std::string& text) {
  std::ofstream output(path, std::ios::binary);
  output << text;
}

std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

int run_compare(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return camera_iq::cmd_spectro_compare(static_cast<int>(argv.size()),
                                        argv.data());
}

std::pair<int, std::string> run_compare_with_stdout(
    const std::vector<std::string>& args) {
  std::ostringstream captured;
  auto* previous = std::cout.rdbuf(captured.rdbuf());
  const int result = run_compare(args);
  std::cout.rdbuf(previous);
  return {result, captured.str()};
}

}  // namespace

void TESTS() {
  const fs::path root =
      fs::temp_directory_path() / "camera_iq_spectro_compare_cmd";
  fs::remove_all(root);
  fs::create_directories(root);
  const fs::path input = root / "series.csv";
  const fs::path json = root / "result.json";
  const fs::path csv = root / "bands.csv";
  write_file(input,
             "series_id,reading_id,wavelength_nm,value\n"
             "reference,r1,0,1\nreference,r1,1,2\nreference,r1,2,1\n"
             "candidate,c1,0,2\ncandidate,c1,2,2\n");

  check(run_compare({input.string(), "--reference", "reference",
                     "--candidate", "candidate", "--common-start", "0",
                     "--common-end", "2", "--common-step", "1",
                     "--exclude", "1", "--offset-min", "0",
                     "--offset-max", "0", "--offset-step", "0.5",
                     "--out-json", json.string(), "--out-csv", csv.string()}) ==
            0,
        "spectro compare cmd: explicit comparison succeeds");
  const std::string json_text = read_file(json);
  check(json_text.find("\"relative_l2_denominator\":\"reference_l2_norm\"") !=
                std::string::npos &&
            json_text.find("\"zero_offset_directional_relative_l2\":") !=
                std::string::npos &&
            json_text.find("\"reference_id\":\"reference\"") !=
                std::string::npos &&
            json_text.find("\"candidate_id\":\"candidate\"") !=
                std::string::npos,
        "spectro compare cmd: JSON binds the directional method and roles");
  check(read_file(csv).find(
            "wavelength_nm,reference_normalized,candidate_normalized") == 0,
        "spectro compare cmd: band CSV is deterministic and inspectable");

  const fs::path missing_json = root / "missing.json";
  check(run_compare({input.string(), "--reference", "missing", "--candidate",
                     "candidate", "--common-start", "0", "--common-end", "2",
                     "--common-step", "1", "--out-json",
                     missing_json.string()}) == 1 &&
            !fs::exists(missing_json),
        "spectro compare cmd: invalid input fails before output creation");
  check(run_compare({input.string(), "--reference", "reference",
                     "--candidate", "candidate", "--common-start", "0",
                     "--common-end", "2", "--common-step", "1",
                     "--out-json", input.string()}) == 2,
        "spectro compare cmd: output cannot alias its input");
  const fs::path invalid_grid_json = root / "invalid-grid.json";
  check(run_compare({input.string(), "--reference", "reference",
                     "--candidate", "candidate", "--common-start", "1",
                     "--common-end", "1", "--common-step", "1",
                     "--out-json", invalid_grid_json.string()}) == 2 &&
            !fs::exists(invalid_grid_json),
        "spectro compare cmd: singleton common grid is refused before allocation");
  check(run_compare({input.string(), "--reference", "reference",
                     "--candidate", "candidate", "--common-start", "0",
                     "--common-end", "1000000", "--common-step", "1",
                     "--out-json", invalid_grid_json.string()}) == 2 &&
            !fs::exists(invalid_grid_json),
        "spectro compare cmd: impractical common grid is refused before allocation");
  check(run_compare({input.string(), "--reference", "reference",
                     "--candidate", "candidate", "--common-start", "-1e308",
                     "--common-end", "1e308", "--common-step", "1",
                     "--out-json", invalid_grid_json.string()}) == 2 &&
            !fs::exists(invalid_grid_json),
        "spectro compare cmd: overflowing grid span is refused");
  const fs::path invalid_sweep_json = root / "invalid-sweep.json";
  check(run_compare({input.string(), "--reference", "reference",
                     "--candidate", "candidate", "--common-start", "0",
                     "--common-end", "2", "--common-step", "1",
                     "--offset-min", "0", "--offset-max", "1e308",
                     "--offset-step", "1", "--out-json",
                     invalid_sweep_json.string()}) == 1 &&
            !fs::exists(invalid_sweep_json),
        "spectro compare cmd: impractical offset sweep is refused without output");
  const auto [help_result, help_text] = run_compare_with_stdout({"--help"});
  check(help_result == 0 &&
            help_text.find("Minimum selected-series wavelength offset") !=
                std::string::npos &&
            help_text.find("Maximum selected-series wavelength offset") !=
                std::string::npos,
        "spectro compare cmd: help names the offset series selected by the caller");

  fs::remove_all(root);
}
