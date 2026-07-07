#include "camera_iq/commands.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::cmd_spectral_smi;
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

double gauss(double x, double mu, double sigma) {
  const double d = (x - mu) / sigma;
  return std::exp(-0.5 * d * d);
}

// "Wavelength (nm),A,B,C" triple CSV over 380-730 nm at 10 nm.
std::string triple_csv() {
  std::string t = "Wavelength (nm),A,B,C\n";
  for (int wl = 380; wl <= 730; wl += 10) {
    const double a = 1.0 * gauss(wl, 600, 40) + 0.35 * gauss(wl, 445, 20);
    const double b = 1.0 * gauss(wl, 555, 45);
    const double c = 1.7 * gauss(wl, 445, 22);
    t += std::to_string(wl) + "," + std::to_string(a) + "," +
         std::to_string(b) + "," + std::to_string(c) + "\n";
  }
  return t;
}

std::string illuminant_csv() {
  std::string t = "Wavelength (nm),Power\n";
  for (int wl = 380; wl <= 730; wl += 10)
    t += std::to_string(wl) + "," + std::to_string(80.0 + 0.05 * (wl - 380)) +
         "\n";
  return t;
}

std::string reflectance_csv() {
  std::string header = "patch_id";
  for (int wl = 380; wl <= 730; wl += 10) header += "," + std::to_string(wl);
  header += "\n";
  const double centers[6] = {600, 450, 550, 500, 680, 520};
  const double widths[6] = {60, 50, 40, 120, 55, 200};
  const double amps[6] = {0.8, 0.7, 0.9, 0.5, 0.85, 0.6};
  std::string rows;
  for (int p = 0; p < 6; ++p) {
    rows += "P" + std::to_string(p + 1);
    for (int wl = 380; wl <= 730; wl += 10)
      rows += "," + std::to_string(0.05 + amps[p] * gauss(wl, centers[p],
                                                          widths[p]));
    rows += "\n";
  }
  return header + rows;
}

int run_smi(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  return cmd_spectral_smi(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_cmd_spectral_smi";
  fs::remove_all(root);

  const fs::path cmf = root / "cmf.csv";
  const fs::path ssf = root / "ssf.csv";      // Luther: identical to CMF
  const fs::path illum = root / "illum.csv";
  const fs::path refl = root / "refl.csv";
  const fs::path out = root / "smi.json";
  write_file(cmf, triple_csv());
  write_file(ssf, triple_csv());
  write_file(illum, illuminant_csv());
  write_file(refl, reflectance_csv());

  const int rc = run_smi(
      {"--ssf-csv", ssf.string(), "--cmf", cmf.string(), "--illuminant-csv",
       illum.string(), "--reflectance-csv", refl.string(), "--camera-model",
       "Luther Reference", "--out", out.string()});
  check(rc == 0, "spectral-smi command: valid Luther run succeeds");
  const std::string json = read_file(out);
  check(json.find("\"camera_model\":\"Luther Reference\"") != std::string::npos,
        "spectral-smi JSON: camera model emitted");
  check(json.find("\"smi\":") != std::string::npos,
        "spectral-smi JSON: smi score emitted");
  check(json.find("\"mean_delta_e_76\":") != std::string::npos,
        "spectral-smi JSON: mean CIELAB error emitted");
  check(json.find("\"white_preserving_mean_delta_e_76\":") != std::string::npos,
        "spectral-smi JSON: white-preserving sensitivity emitted");
  check(json.find("\"white_preserving_delta_smi\":") != std::string::npos,
        "spectral-smi JSON: white-preserving SMI delta emitted");
  check(json.find("\"white_preserving_white_delta_e_76\":") !=
            std::string::npos,
        "spectral-smi JSON: white-preserving white error emitted");
  check(json.find("\"patch_count\":6") != std::string::npos,
        "spectral-smi JSON: patch count reflects reflectance rows");
  check(json.find("ISO-17321-style only when the supplied slope and test "
                  "colours match the standard") != std::string::npos,
        "spectral-smi JSON: interpretation does not overclaim arbitrary test set");
  check(json.find("Annex B optimization convention") != std::string::npos,
        "spectral-smi JSON: interpretation does not overclaim bit-exact ISO");
  check(json.find("white-preserving sensitivity") != std::string::npos,
        "spectral-smi JSON: interpretation explains constrained-fit bound");

  // Luther camera (SSF == CMF) scores essentially 100.
  const std::size_t k = json.find("\"smi\":");
  double smi = 0.0;
  if (k != std::string::npos) smi = std::stod(json.substr(k + 6));
  check(smi > 99.0, "spectral-smi: Luther camera scores ~100 end-to-end");

  // Missing reflectance is rejected at arg-validation time.
  check(run_smi({"--ssf-csv", ssf.string(), "--cmf", cmf.string(),
                 "--illuminant-csv", illum.string(), "--camera-model", "X",
                 "--out", (root / "n1.json").string()}) == 2,
        "spectral-smi command: reflectance argument required");

  // Unknown argument is rejected.
  check(run_smi({"--bogus", "1"}) == 2,
        "spectral-smi command: unknown argument rejected");

  // A structurally broken reflectance file fails at load (exit 1), no output.
  const fs::path bad = root / "bad.csv";
  const fs::path bad_out = root / "bad.json";
  write_file(bad, "not_a_header,1,2\nrow,0.1\n");
  check(run_smi({"--ssf-csv", ssf.string(), "--cmf", cmf.string(),
                 "--illuminant-csv", illum.string(), "--reflectance-csv",
                 bad.string(), "--camera-model", "X", "--out",
                 bad_out.string()}) == 1,
        "spectral-smi command: malformed reflectance exits 1");
  check(!fs::exists(bad_out),
        "spectral-smi command: failure writes no output");

  fs::remove_all(root);
}
