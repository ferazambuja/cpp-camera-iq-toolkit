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

int run_exposure_response(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return camera_iq::cmd_exposure_response(static_cast<int>(argv.size()),
                                          argv.data());
}

std::string read_file(const fs::path& path) {
  std::ifstream is(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(is),
                     std::istreambuf_iterator<char>());
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_expresp_cmd";
  fs::remove_all(root);
  const fs::path dataset = root / "dataset";
  fs::create_directories(dataset);
  // A couple of raw names that cannot form a series (single shutter each);
  // the command must still succeed and write JSON with zero series.
  write_file(dataset / "NIKON D800_i100_s1-40_2.NEF");
  write_file(dataset / "NIKON D800_i200_s1-80_11.NEF");

  const fs::path out = root / "out.json";
  const int ok = run_exposure_response(
      {dataset.string(), "--series-min", "3", "--out", out.string()});
  check(ok == 0, "exposure-response cmd: direct root with zero series succeeds");
  const std::string json = read_file(out);
  // Direct-path mode must never echo the absolute dataset root into evidence
  // JSON — same privacy contract as oecf-stepchart's dataset-root label.
  check(json.find("dataset-root:dataset") != std::string::npos,
        "exposure-response cmd: direct root mode emits basename label");
  check(json.find(dataset.string()) == std::string::npos,
        "exposure-response cmd: direct root mode does not leak absolute root");
  check(run_exposure_response(
            {dataset.string(), "--subdir", (root / "escape").string()}) == 2,
        "exposure-response cmd: absolute subdir rejected");

  fs::remove_all(root);
}
