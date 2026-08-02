#include "camera_iq/commands.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "camera_iq/sha256.hpp"
#include "harness.hpp"
#include "spectro_mat_fixture.hpp"

using camera_iq::cmd_spectro_ingest;
using camera_iq::sha256_hex;
using test::check;

namespace {

struct TempTree {
  TempTree() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = std::filesystem::temp_directory_path() /
           ("camera_iq_cmd_spectro_" + std::to_string(stamp));
    std::filesystem::create_directories(root / "dataset");
  }
  ~TempTree() { std::filesystem::remove_all(root); }
  std::filesystem::path root;
};

void write_bytes(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

int run_command(const std::vector<std::string> &args) {
  std::vector<char *> argv;
  for (const std::string &arg : args) {
    argv.push_back(const_cast<char *>(arg.c_str()));
  }
  return cmd_spectro_ingest(static_cast<int>(argv.size()), argv.data());
}

bool contains(const std::string &text, const std::string &needle) {
  return text.find(needle) != std::string::npos;
}

} // namespace

void TESTS() {
  check(run_command({}) == 2,
        "spectro-ingest command: dataset root or ID is required");
  check(run_command({"dataset", "--unknown"}) == 2,
        "spectro-ingest command: unknown options are rejected");

  TempTree tree;
  const std::filesystem::path dataset = tree.root / "dataset";
  const std::string mat = spectro_test::measurement_mat();
  write_bytes(dataset / "reading.mat", mat);
  const std::string ledger =
      "group_id,repeat_index,canonical_path,sha256,alias_paths\n"
      "scene_01,1,reading.mat," +
      sha256_hex(mat) + ",\n";
  const std::filesystem::path ledger_path = tree.root / "ledger.csv";
  write_bytes(ledger_path, ledger);
  const std::filesystem::path cmf_path = tree.root / "cmf.csv";
  write_bytes(cmf_path, "Wavelength (nm),X,Y,Z\n"
                        "380,1,0,0\n"
                        "382,0,1,0\n"
                        "384,0,0,1\n");

  const std::filesystem::path json_path = tree.root / "result.json";
  const std::filesystem::path groups_path = tree.root / "groups.csv";
  const std::filesystem::path spectra_path = tree.root / "spectra.csv";
  const std::filesystem::path readings_path = tree.root / "readings.csv";
  check(run_command({dataset.string(), "--ledger", ledger_path.string(),
                     "--cmf", cmf_path.string(), "--out", json_path.string(),
                     "--groups-csv", groups_path.string(), "--spectra-csv",
                     spectra_path.string(), "--readings-csv",
                     readings_path.string()}) == 0,
        "spectro-ingest command: verified archive emits all artifacts");

  const std::string json = read_bytes(json_path);
  check(
      contains(json, "\"schema_version\":1") &&
          contains(json, "\"sample_weighting\":\"uniform_equal_weight\"") &&
          contains(json,
                   "\"variation_label\":\"within_group_observed_variation\"") &&
          contains(json, "\"cause\":\"unresolved\"") &&
          contains(json, "\"scale_source\":\"derived_from_recorded_xyz\"") &&
          contains(json, "\"scale_value\":5") &&
          contains(json, "\"singleton_reason\":\"one measurement does not "
                         "establish variation\""),
      "spectro-ingest JSON: method and singleton limits are explicit");
  check(!contains(json, tree.root.string()),
        "spectro-ingest JSON: local absolute paths are not serialized");

  check(contains(read_bytes(groups_path),
                 "group_id,count,mean_spectral_integral") &&
            contains(read_bytes(groups_path), "scene_01,1,12"),
        "spectro-ingest group CSV: absolute level is inspectable");
  check(contains(read_bytes(spectra_path), "mean_absolute_radiance") &&
            contains(read_bytes(spectra_path), "scene_01,380,1"),
        "spectro-ingest spectra CSV: absolute and normalized spectra are "
        "emitted");
  check(
      contains(read_bytes(readings_path), "recorded_total_radiance") &&
          contains(read_bytes(readings_path), "computed_x") &&
          contains(read_bytes(readings_path), "radiance_binary64_le_sha256") &&
          contains(read_bytes(readings_path), "reading.mat"),
      "spectro-ingest reading CSV: source identity and recorded metadata are "
      "emitted");

  check(run_command({dataset.string(), "--ledger", ledger_path.string(),
                     "--out", (dataset / "would_overwrite.json").string()}) ==
            2,
        "spectro-ingest command: outputs inside the source root are rejected");
  check(run_command({dataset.string(), "--ledger", ledger_path.string(),
                     "--out", json_path.string(), "--groups-csv",
                     json_path.string()}) == 2,
        "spectro-ingest command: output paths must be distinct");
}
