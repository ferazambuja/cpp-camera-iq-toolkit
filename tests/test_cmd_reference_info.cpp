#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream os(path, std::ios::binary);
  os << text;
}

std::string read_file(const fs::path& path) {
  std::ifstream is(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(is),
                     std::istreambuf_iterator<char>());
}

int run_reference_info(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return camera_iq::cmd_reference_info(static_cast<int>(argv.size()),
                                       argv.data());
}

}  // namespace

void TESTS() {
  const fs::path root =
      fs::temp_directory_path() / "camera_iq_cmd_reference_info";
  fs::remove_all(root);
  const fs::path reference = root / "direct-reference.csv";
  const fs::path out = root / "reference-info.json";
  write_file(reference,
             "patch_id,380,390,400\n"
             "A1,0.10,0.20,0.30\n"
             "B1,0.40,0.50,0.60\n");

  test::check(run_reference_info(
                  {reference.string(), "--out", out.string()}) == 0,
              "reference-info command: direct spectral reference succeeds");
  const std::string json = read_file(out);
  test::check(json.find("\"path\":\"external:direct-reference.csv\"") !=
                  std::string::npos,
              "reference-info JSON: direct reference publishes scoped basename");
  test::check(json.find(root.string()) == std::string::npos,
              "reference-info JSON: local absolute directory is not published");

  fs::remove_all(root);
}
