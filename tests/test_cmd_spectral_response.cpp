#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::cmd_spectral_response;
using test::check;

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

std::string response_csv() {
  std::string text = "Wavelength (nm),Red,Green,Blue\n";
  for (int i = 0; i < 48; ++i) {
    const int wavelength = 360 + i * 10;
    text += std::to_string(wavelength) + ",0.25,";
    text += (i == 20 ? "1.0" : "0.5");
    text += ",0.125\n";
  }
  return text;
}

std::string spd_csv(const std::string& bad_value = {}) {
  std::string text = " - [21:08:18] New Scan,Voltage (V) - [21:08:18] New Scan\n";
  for (int i = 0; i < 48; ++i) {
    const double wavelength = 359.993 + i * 10.0;
    const std::string voltage = (!bad_value.empty() && i == 7) ? bad_value
                                                               : "0.00001";
    text += std::to_string(wavelength) + "," + voltage + "\n";
  }
  return text;
}

int run_spectral_response(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return cmd_spectral_response(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_cmd_spectral_response";
  fs::remove_all(root);

  const fs::path response = root / "response.csv";
  const fs::path spd = root / "spd.csv";
  const fs::path out = root / "spectral_response.json";
  write_file(response, response_csv());
  write_file(spd, spd_csv());

  const int rc = run_spectral_response(
      {"--response-csv", response.string(), "--spd-csv", spd.string(),
       "--camera-model", "Canon EOS 5D Mark II", "--dataset-id",
       "spectral_sensitivity_2016_2017", "--archive-subset",
       "canon_5d2/2016_11_21_5D2_Monochromator_OK", "--out", out.string()});
  check(rc == 0, "spectral-response command: valid run succeeds");
  const std::string json = read_file(out);
  check(json.find("\"camera_model\":\"Canon EOS 5D Mark II\"") !=
            std::string::npos,
        "spectral-response JSON: camera model emitted");
  check(json.find("\"dataset_id\":\"spectral_sensitivity_2016_2017\"") !=
            std::string::npos,
        "spectral-response JSON: dataset id emitted");
  check(json.find("\"validation_tier\":\"legacy_fidelity_only\"") !=
            std::string::npos,
        "spectral-response JSON: legacy fidelity tier emitted");
  check(json.find("\"response_rgb\"") != std::string::npos,
        "spectral-response JSON: response object emitted");
  check(json.find("\"line_spd\"") != std::string::npos,
        "spectral-response JSON: line SPD emitted");

  check(run_spectral_response({"--response-csv", response.string(),
                               "--spd-csv", spd.string()}) == 2,
        "spectral-response command: provenance args required");

  check(run_spectral_response(
            {"--response-csv", response.string(), "--spd-csv", spd.string(),
             "--camera-model", "Canon EOS 5D Mark II", "--dataset-id",
             "spectral_sensitivity_2016_2017", "--archive-subset",
             "canon_5d2/2016_11_21_5D2_Monochromator_OK", "--ssf-csv-out",
             (root / "ssf.csv").string(), "--out",
             (root / "noraw.json").string()}) == 2,
        "spectral-response command: --ssf-csv-out requires --raw-dir (no "
        "toolkit SSF without extraction)");

  check(run_spectral_response(
            {"--response-csv", response.string(), "--spd-csv", spd.string(),
             "--camera-model", "Canon EOS 5D Mark II", "--dataset-id",
             "spectral_sensitivity_2016_2017", "--archive-subset",
             "canon_5d2/2016_11_21_5D2_Monochromator_OK", "--raw-dir",
             (root / "raw").string(), "--out", (root / "raw.json").string()}) ==
            2,
        "spectral-response command: raw-dir requires dark-raw");

  check(run_spectral_response(
            {"--response-csv", response.string(), "--spd-csv", spd.string(),
             "--camera-model", "Canon EOS 5D Mark II", "--dataset-id",
             "spectral_sensitivity_2016_2017", "--archive-subset",
             "canon_5d2/2016_11_21_5D2_Monochromator_OK", "--dark-raw",
             (root / "dark.CR2").string(), "--out",
             (root / "raw2.json").string()}) == 2,
        "spectral-response command: dark-raw requires raw-dir");

  check(run_spectral_response(
            {"--response-csv", response.string(), "--spd-csv", spd.string(),
             "--camera-model", "Canon EOS 5D Mark II", "--dataset-id",
             "spectral_sensitivity_2016_2017", "--archive-subset",
             "canon_5d2/2016_11_21_5D2_Monochromator_OK", "--raw-dir",
             (root / "raw").string(), "--dark-raw",
             (root / "dark.CR2").string(), "--out",
             (root / "raw3.json").string()}) == 1,
        "spectral-response command: raw extraction syntax accepted before RAW I/O");

  const fs::path bad_spd = root / "bad_spd.csv";
  const fs::path bad_out = root / "bad.json";
  write_file(bad_spd, spd_csv("0"));
  check(run_spectral_response(
            {"--response-csv", response.string(), "--spd-csv",
             bad_spd.string(), "--camera-model", "Canon EOS 5D Mark II",
             "--dataset-id", "spectral_sensitivity_2016_2017",
             "--archive-subset",
             "canon_5d2/2016_11_21_5D2_Monochromator_OK", "--out",
             bad_out.string()}) == 1,
        "spectral-response command: validation failure exits 1");
  check(!fs::exists(bad_out),
        "spectral-response command: validation failure writes no output");

  fs::remove_all(root);
}
