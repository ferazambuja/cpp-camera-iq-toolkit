#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;

using test::check;

namespace {

void write_file(const fs::path& path, const std::string& contents) {
  std::ofstream os(path, std::ios::binary);
  os << contents;
}

std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

int run_gamut_map(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return camera_iq::cmd_gamut_map(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_gamut_map_cmd";
  fs::remove_all(root);
  fs::create_directories(root);
  const fs::path input = root / "synthetic-p3.csv";
  write_file(input,
             "id,r,g,b\n"
             "p3_red,1,0,0\n"
             "gray,0.5,0.5,0.5\n"
             "p3_yellow,1,1,0\n");

  const fs::path json_path = root / "result.json";
  const fs::path csv_path = root / "result.csv";
  check(run_gamut_map({input.string(), "--out-json", json_path.string(),
                       "--out-csv", csv_path.string()}) == 0,
        "gamut-map cmd: radial batch succeeds");
  const std::string json = read_file(json_path);
  const std::string csv = read_file(csv_path);
  check(json.find("\"input_label\":\"synthetic-p3.csv\"") !=
            std::string::npos,
        "gamut-map cmd: output publishes basename only");
  check(json.find(input.string()) == std::string::npos,
        "gamut-map cmd: absolute input path is not serialized");
  check(json.find("\"out_of_gamut_count\":2") != std::string::npos,
        "gamut-map cmd: JSON aggregate is reproducible");
  check(json.find("\"input_sha256\":\"\"") == std::string::npos,
        "gamut-map cmd: input bytes are hashed");
  check(csv.find("p3_red,1,0,0,false,true") != std::string::npos,
        "gamut-map cmd: CSV agrees on P3-red classification");

  const fs::path soft_path = root / "soft.json";
  check(run_gamut_map({input.string(), "--intent", "soft-knee", "--knee",
                       "0.7", "--out-json", soft_path.string()}) == 0,
        "gamut-map cmd: experimental soft intent succeeds");
  const std::string soft = read_file(soft_path);
  check(soft.find("experimental_CIELAB_protected_core") !=
            std::string::npos,
        "gamut-map cmd: experimental status is explicit");
  check(soft.find("\"knee_fraction_of_destination_boundary\":0.7") !=
            std::string::npos,
        "gamut-map cmd: knee serialized");

  const fs::path oklch_path = root / "oklch.json";
  check(run_gamut_map({input.string(), "--intent", "oklch-radial",
                       "--out-json", oklch_path.string()}) == 0,
        "gamut-map cmd: OkLCh radial comparison succeeds");
  check(read_file(oklch_path).find("fixed_OkLCh_radial_boundary_clip") !=
            std::string::npos,
        "gamut-map cmd: OkLCh radial identity is serialized");

  const fs::path css_path = root / "css-local-minde.json";
  check(run_gamut_map({input.string(), "--intent", "css-local-minde",
                       "--out-json", css_path.string()}) == 0,
        "gamut-map cmd: dated CSS Local MINDE comparison succeeds");
  check(read_file(css_path).find(
            "CSS_Color_4_2026-07-28_binary_search_local_MINDE") !=
            std::string::npos,
        "gamut-map cmd: dated CSS algorithm is serialized");

  check(run_gamut_map({input.string(), "--intent", "unknown"}) == 2,
        "gamut-map cmd: unknown intent rejected");
  check(run_gamut_map({input.string(), "--source", "adobe-rgb"}) == 2,
        "gamut-map cmd: unknown source rejected");
  check(run_gamut_map({input.string(), "--knee", "1.1"}) == 2,
        "gamut-map cmd: invalid knee rejected");
  check(run_gamut_map({input.string(), "--knee", "1"}) == 2,
        "gamut-map cmd: discontinuous knee rejected");
  check(run_gamut_map({input.string(), "--out-json", input.string()}) == 2,
        "gamut-map cmd: input overwrite rejected");
  check(run_gamut_map({input.string(), "--out-json", json_path.string(),
                       "--out-csv", json_path.string()}) == 2,
        "gamut-map cmd: aliased outputs rejected");

  const fs::path nan_input = root / "nan.csv";
  write_file(nan_input, "id,r,g,b\ninvalid,nan,0,0\n");
  check(run_gamut_map({nan_input.string()}) == 1,
        "gamut-map cmd: NaN CSV component rejected");
  const fs::path infinity_input = root / "infinity.csv";
  write_file(infinity_input, "id,r,g,b\ninvalid,inf,0,0\n");
  check(run_gamut_map({infinity_input.string()}) == 1,
        "gamut-map cmd: infinite CSV component rejected");
  check(run_gamut_map({}) == 2, "gamut-map cmd: missing input rejected");

  fs::remove_all(root);
}
