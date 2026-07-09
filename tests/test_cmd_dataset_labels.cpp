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

std::string read_file(const fs::path& path) {
  std::ifstream is(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(is),
                     std::istreambuf_iterator<char>());
}

int run_cmd(int (*cmd)(int, char**), const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return cmd(static_cast<int>(argv.size()), argv.data());
}

void check_sanitized_dataset_label(const std::string& doc,
                                   const fs::path& dataset,
                                   const std::string& expected_label,
                                   const std::string& name) {
  check(doc.find(expected_label) != std::string::npos,
        name + ": emits sanitized direct-root dataset label");
  check(doc.find(dataset.string()) == std::string::npos,
        name + ": does not leak absolute direct-root path");
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_cmd_labels";
  fs::remove_all(root);
  const fs::path dataset = root / "dataset";
  fs::create_directories(dataset);

  write_file(dataset / "Images" / "Dark Frame" /
             "Dark_Frame_f8.0_1:60_ISO200_DSCF0001.RAF");
  write_file(dataset / "Images" / "Sphere" /
             "Sphere_f8.0_1:100_ISO200_DSCF0002.RAF");
  write_file(dataset / "shape.csv", "a,b\n1,2\n");

  {
    const fs::path out = root / "manifest.json";
    const int rc = run_cmd(camera_iq::cmd_manifest,
                           {dataset.string(), "--no-exif", "--out",
                            out.string()});
    check(rc == 0, "manifest direct-root fixture succeeds");
    check_sanitized_dataset_label(read_file(out), dataset,
                                  "\"root\":\"dataset-root:dataset\"",
                                  "manifest");
    check(run_cmd(camera_iq::cmd_manifest,
                  {dataset.string(), "--subdir", (root / "escape").string(),
                   "--no-exif"}) == 2,
          "manifest direct-root fixture rejects absolute subdir");
    check(run_cmd(camera_iq::cmd_manifest,
                  {dataset.string(), "--subdir", "../escape", "--no-exif"}) ==
              2,
          "manifest rejects parent-path subdir traversal");
  }

  {
    const fs::path out = root / "oecf-fit.json";
    const int rc = run_cmd(camera_iq::cmd_oecf_fit,
                           {dataset.string(), "--series-min", "3", "--out",
                            out.string()});
    check(rc == 0, "oecf-fit direct-root fixture succeeds");
    check_sanitized_dataset_label(read_file(out), dataset,
                                  "\"root\":\"dataset-root:dataset\"",
                                  "oecf-fit");
    check(run_cmd(camera_iq::cmd_oecf_fit,
                  {dataset.string(), "--subdir", "../escape"}) == 2,
          "oecf-fit rejects parent-path subdir traversal");
  }

  {
    const fs::path out = root / "dark-calibration.json";
    const int rc = run_cmd(camera_iq::cmd_dark_calibration,
                           {dataset.string(), "--subdir", "Images/Dark Frame",
                            "--out", out.string()});
    check(rc == 0, "dark-calibration direct-root fixture succeeds");
    check_sanitized_dataset_label(
        read_file(out), dataset,
        "\"root\":\"dataset-root:dataset/Images/Dark Frame\"",
        "dark-calibration");
    check(run_cmd(camera_iq::cmd_dark_calibration,
                  {dataset.string(), "--subdir", "../escape"}) == 2,
          "dark-calibration rejects parent-path subdir traversal");
  }

  {
    const fs::path out = root / "noise.json";
    const int rc = run_cmd(camera_iq::cmd_noise,
                           {dataset.string(), "--subdir", "Images/Dark Frame",
                            "--out", out.string()});
    check(rc == 0, "noise direct-root fixture succeeds");
    check_sanitized_dataset_label(
        read_file(out), dataset,
        "\"root\":\"dataset-root:dataset/Images/Dark Frame\"", "noise");
    check(run_cmd(camera_iq::cmd_noise,
                  {dataset.string(), "--subdir", "Images/../../escape"}) == 2,
          "noise rejects parent-path subdir traversal");
  }

  fs::remove_all(root);
}
