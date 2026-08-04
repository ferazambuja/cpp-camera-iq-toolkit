#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;
using test::check;

namespace {

int run_audit(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return camera_iq::cmd_cam16_equation_audit(
      static_cast<int>(argv.size()), argv.data());
}

std::string read_file(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

}  // namespace

void TESTS() {
  const fs::path root =
      fs::temp_directory_path() / "camera_iq_cam16_equation_audit_cmd";
  fs::remove_all(root);
  fs::create_directories(root);
  const fs::path json = root / "audit.json";
  const fs::path csv = root / "audit.csv";

  check(run_audit({"--out-json", json.string(), "--out-csv", csv.string()}) ==
            0,
        "CAM16 audit cmd: deterministic export succeeds");
  check(read_file(json).find("equation_level_not_perceptual_validation") !=
            std::string::npos,
        "CAM16 audit cmd: JSON states the evidence boundary");
  check(read_file(csv).find("normalized_brightness") != std::string::npos,
        "CAM16 audit cmd: CSV contains the brightness curve");
  check(run_audit({"--out-json", json.string(), "--out-csv", json.string()}) ==
            2,
        "CAM16 audit cmd: aliased outputs are rejected");
  check(run_audit({"--unknown"}) == 2,
        "CAM16 audit cmd: unknown option is rejected");
  check(run_audit({"--help"}) == 0, "CAM16 audit cmd: help succeeds");

  fs::remove_all(root);
}
