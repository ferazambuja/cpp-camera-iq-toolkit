#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

using test::check;

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& p, const std::string& text) {
  std::ofstream os(p, std::ios::binary);
  os << text;
}

int run_closure(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  return camera_iq::cmd_spectral_closure(static_cast<int>(argv.size()),
                                         argv.data());
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_closure_cmd";
  fs::remove_all(root);
  fs::create_directories(root);

  // Exact synthetic where measured == 10 * (SSF . E . reflectance):
  //   raw predicted p1 = (1,0.2,1), p2 = (0.2,1,1);
  //   white flat = (1.2,1.2,2). Every channel is above dark so the command
  //   fixture exercises dark subtraction without tripping the below-dark guard.
  write_file(root / "ssf.csv",
             "Wavelength (nm),Red,Green,Blue\n500,1,0.2,1\n"
             "510,0.2,1,1\n");
  write_file(root / "illuminant.txt", "wl\tvalue\n500\t1\n510\t1\n");

  // Reflectance CGATS with paired SPECTRAL_NM/SPECTRAL_DEC and CR-ONLY line
  // endings (the real 2016 SpectraShop export is old-Mac CR).
  const std::string refl =
      "CGATS.17\r"
      "BEGIN_DATA_FORMAT\r"
      "SAMPLE_ID\tSPECTRAL_NM\tSPECTRAL_DEC\tSPECTRAL_NM\tSPECTRAL_DEC\r"
      "END_DATA_FORMAT\r"
      "BEGIN_DATA\r"
      "p1\t500\t1\t510\t0\r"
      "p2\t500\t0\t510\t1\r"
      "END_DATA\r";
  write_file(root / "reflectance.txt", refl);

  const std::string target =
      "CGATS.17\n"
      "DESCRIPTOR \"OELevels=100,100,100,100\"\n"
      "BEGIN_DATA_FORMAT\n"
      "SAMPLE_ID\tSAMPLE_NAME\tRGB_R\tRGB_G\tRGB_B\n"
      "END_DATA_FORMAT\n"
      "BEGIN_DATA\n"
      "1\tp1\t11\t3\t11\n"
      "2\tp2\t3\t11\t11\n"
      "END_DATA\n";
  write_file(root / "target.txt", target);

  const std::string white =
      "CGATS.17\n"
      "BEGIN_DATA_FORMAT\n"
      "SAMPLE_ID\tSAMPLE_NAME\tRGB_R\tRGB_G\tRGB_B\n"
      "END_DATA_FORMAT\n"
      "BEGIN_DATA\n"
      "1\tp1\t13\t13\t21\n"
      "2\tp2\t13\t13\t21\n"
      "END_DATA\n";
  write_file(root / "white.txt", white);

  const std::string dark =
      "CGATS.17\n"
      "BEGIN_DATA_FORMAT\n"
      "SAMPLE_ID\tSAMPLE_NAME\tRGB_R\tRGB_G\tRGB_B\n"
      "END_DATA_FORMAT\n"
      "BEGIN_DATA\n"
      "1\tp1\t1\t1\t1\n"
      "2\tp2\t1\t1\t1\n"
      "END_DATA\n";
  write_file(root / "dark.txt", dark);

  const fs::path out = root / "closure.json";
  const int rc = run_closure(
      {"--ssf-csv", (root / "ssf.csv").string(), "--illuminant",
       (root / "illuminant.txt").string(), "--reflectance",
       (root / "reflectance.txt").string(), "--target-rgb",
       (root / "target.txt").string(), "--white-rgb",
       (root / "white.txt").string(), "--dark-rgb",
       (root / "dark.txt").string(), "--camera-model", "TestCam", "--dataset-id",
       "test_ds", "--archive-subset", "sub", "--out", out.string()});

  check(rc == 0, "closure cmd: gate-passing run returns success exit");

  std::ifstream is(out, std::ios::binary);
  const std::string json((std::istreambuf_iterator<char>(is)),
                         std::istreambuf_iterator<char>());
  check(json.find("\"validation_tier\":\"tier3_physical_closure\"") !=
            std::string::npos,
        "closure cmd: emits the tier-3 validation tier");
  check(json.find("\"passes\":true") != std::string::npos,
        "closure cmd: white-card gate passes on the CR-only synthetic set");
  check(json.find("\"matched_patches\":2") != std::string::npos,
        "closure cmd: matches both CR-only reflectance patches to measured RGB");
  check(json.find("\"dark_rgb_subtracted\":true") != std::string::npos,
        "closure cmd: records dark-RGB subtraction");
  check(json.find("\"target_dark_subtracted_patch_count\":2") !=
            std::string::npos,
        "closure cmd: subtracts dark from both target patches");
  check(json.find("\"target_saturated_excluded_patch_count\":0") !=
            std::string::npos,
        "closure cmd: reports saturated-patch exclusions");
  check(json.find("\"scale_mode\":\"global_single_k\"") != std::string::npos,
        "closure cmd: records the single-global-k scale mode");
  check(json.find("\"patches\":[") != std::string::npos,
        "closure cmd: emits per-patch measured/predicted/residual rows");

  // A mismatched illuminant makes the white ratios inconsistent -> gate fails,
  // command returns nonzero. Reuse everything but a red-skewed white card.
  write_file(root / "white_bad.txt",
             "CGATS.17\nBEGIN_DATA_FORMAT\n"
             "SAMPLE_ID\tSAMPLE_NAME\tRGB_R\tRGB_G\tRGB_B\n"
             "END_DATA_FORMAT\nBEGIN_DATA\n1\tp1\t81\t13\t21\n"
             "2\tp2\t81\t13\t21\nEND_DATA\n");
  const int rc_fail = run_closure(
      {"--ssf-csv", (root / "ssf.csv").string(), "--illuminant",
       (root / "illuminant.txt").string(), "--reflectance",
       (root / "reflectance.txt").string(), "--target-rgb",
       (root / "target.txt").string(), "--white-rgb",
       (root / "white_bad.txt").string(), "--dark-rgb",
       (root / "dark.txt").string(), "--camera-model", "TestCam", "--dataset-id",
       "test_ds", "--archive-subset", "sub", "--out",
       (root / "closure_fail.json").string()});
  check(rc_fail == 1, "closure cmd: gate failure returns nonzero exit");

  // Mean-near-OE target rows are excluded and counted instead of silently
  // entering the closure fit.
  write_file(root / "target_sat.txt",
             "CGATS.17\nDESCRIPTOR \"OELevels=20,20,20,20\"\n"
             "BEGIN_DATA_FORMAT\nSAMPLE_ID\tSAMPLE_NAME\tRGB_R\tRGB_G\tRGB_B\n"
             "END_DATA_FORMAT\nBEGIN_DATA\n"
             "1\tp1\t20\t3\t11\n"
             "2\tp2\t3\t11\t11\n"
             "END_DATA\n");
  const fs::path sat_out = root / "closure_sat.json";
  const int rc_sat = run_closure(
      {"--ssf-csv", (root / "ssf.csv").string(), "--illuminant",
       (root / "illuminant.txt").string(), "--reflectance",
       (root / "reflectance.txt").string(), "--target-rgb",
       (root / "target_sat.txt").string(), "--white-rgb",
       (root / "white.txt").string(), "--dark-rgb",
       (root / "dark.txt").string(), "--camera-model", "TestCam", "--dataset-id",
       "test_ds", "--archive-subset", "sub", "--out", sat_out.string()});
  check(rc_sat == 0,
        "closure cmd: run can continue after excluding a saturated target patch");
  std::ifstream sat_is(sat_out, std::ios::binary);
  const std::string sat_json((std::istreambuf_iterator<char>(sat_is)),
                             std::istreambuf_iterator<char>());
  check(sat_json.find("\"patch_count\":1") != std::string::npos,
        "closure cmd: saturated target patch is excluded from closure patches");
  check(sat_json.find("\"matched_patches\":2") != std::string::npos,
        "closure cmd: matched count records pre-exclusion target matches");
  check(sat_json.find("\"target_saturated_excluded_patch_count\":1") !=
            std::string::npos,
        "closure cmd: saturated target exclusion count is serialized");
}
