#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::cmd_ccm_fit;
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

int run_ccm_fit(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return cmd_ccm_fit(static_cast<int>(argv.size()), argv.data());
}

std::string good_config(const fs::path& root, const fs::path& reference,
                        const fs::path& camera_rgb) {
  return R"json({
    "datasets": {
      "fixture": {
        "root": ")json" + root.generic_string() + R"json(",
        "description": "synthetic CCM fixture",
        "capture_project": "Synthetic capture",
        "capture_year": "2026",
        "timeline_note": "Synthetic reference and capture are intentionally separate.",
        "color_reference": {
          "id": "synthetic_ref",
          "role": "compatible_sg_spectral",
          "format": "camera_iq_spectral_csv",
          "path": ")json" + reference.generic_string() + R"json(",
          "selection_basis": "project_provenance_not_camera_date",
          "source": "synthetic_reference_source",
          "reference_project": "Synthetic reference",
          "reference_year": "2025",
          "physical_chart_identity": "compatible_reference_not_proven_same_physical_chart",
          "illuminant": "not_applicable_reflectance",
          "observer": "not_applicable_reflectance",
          "unit": "spectral_reflectance",
          "numbering_order": "synthetic_reference_order",
          "expected_patch_count": 6,
          "expected_band_count": 4,
          "first_wavelength_nm": 430,
          "last_wavelength_nm": 650,
          "min_reflectance": 0,
          "max_reflectance": 1,
          "pairing_rgb_path": ")json" + camera_rgb.generic_string() + R"json(",
          "pairing_min_luminance_correlation": 0.90,
          "pairing_min_red_green_correlation": 0.80,
          "pairing_min_blue_green_correlation": 0.80
        }
      }
    }
  })json";
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_cmd_ccm_fit";
  fs::remove_all(root);
  fs::create_directories(root);

  const fs::path reference = root / "reference.csv";
  const fs::path camera_rgb = root / "camera_rgb.csv";
  const fs::path illuminant = root / "illuminant.csv";
  const fs::path config = root / "datasets.local.json";
  const fs::path out = root / "ccm_fit.json";

  write_file(reference,
             "patch_id,430,500,560,650\n"
             "D1,0.01,0.01,0.01,0.01\n"
             "D2,0.02,0.02,0.02,0.02\n"
             "W1,0.80,0.80,0.80,0.80\n"
             "R1,0.05,0.08,0.07,0.80\n"
             "G1,0.05,0.70,0.75,0.10\n"
             "B1,0.70,0.10,0.08,0.05\n");
  write_file(camera_rgb,
             "0.01,0.01,0.01\n"
             "0.02,0.02,0.02\n"
             "0.80,0.80,0.80\n"
             "0.80,0.075,0.05\n"
             "0.10,0.725,0.05\n"
             "0.05,0.09,0.70\n");
  write_file(illuminant,
             "430,1\n"
             "500,1\n"
             "560,1\n"
             "650,1\n");
  write_file(config, good_config(root, reference, camera_rgb));

  const int rc = run_ccm_fit({"fixture", "--config", config.string(),
                              "--illuminant-spd", illuminant.string(),
                              "--exclude-ref-lightness-below", "25", "--out",
                              out.string()});
  check(rc == 0, "ccm-fit command: exclusion run succeeds");
  const std::string json = read_file(out);
  check(json.find("\"timeline_provenance\"") != std::string::npos,
        "ccm-fit JSON: timeline provenance emitted");
  check(json.find("\"capture_project\":\"Synthetic capture\"") !=
            std::string::npos,
        "ccm-fit JSON: capture project emitted");
  check(json.find("\"reference_year\":\"2025\"") != std::string::npos,
        "ccm-fit JSON: reference year emitted");
  check(json.find("\"physical_chart_identity\":"
                  "\"compatible_reference_not_proven_same_physical_chart\"") !=
            std::string::npos,
        "ccm-fit JSON: physical chart identity emitted");
  check(json.find("\"reference_numbering_order\":"
                  "\"synthetic_reference_order\"") != std::string::npos,
        "ccm-fit JSON: numbering order emitted");
  check(json.find("\"lightness_exclusion\":{\"enabled\":true") !=
            std::string::npos,
        "ccm-fit JSON: lightness exclusion enabled");
  check(json.find("\"kept_patch_count\":4") != std::string::npos,
        "ccm-fit JSON: kept patch count");
  check(json.find("\"excluded_patch_count\":2") != std::string::npos,
        "ccm-fit JSON: excluded patch count");
  check(json.find("\"excluded_reference_patch_ids\":[\"D1\",\"D2\"]") !=
            std::string::npos,
        "ccm-fit JSON: excluded reference patch ids");
  check(json.find("\"evaluations\"") != std::string::npos,
        "ccm-fit JSON: evaluations block emitted");
  check(json.find("\"baseline_all_patch_fit\"") != std::string::npos,
        "ccm-fit JSON: baseline all-patch fit emitted");
  check(json.find("\"excluded_patches_with_fit_matrix\"") != std::string::npos,
        "ccm-fit JSON: excluded-patch evaluation emitted");
  check(json.find("\"baseline_cross_validation\"") != std::string::npos,
        "ccm-fit JSON: baseline CV emitted");

  const fs::path stale_config = root / "stale.local.json";
  write_file(stale_config,
             R"json({
    "datasets": {
      "fixture": {
        "root": ")json" + root.generic_string() + R"json(",
        "description": "stale config",
        "color_reference": {
          "id": "synthetic_ref",
          "role": "compatible_sg_spectral",
          "format": "camera_iq_spectral_csv",
          "path": ")json" + reference.generic_string() + R"json(",
          "selection_basis": "project_provenance_not_camera_date",
          "source": "synthetic_reference_source",
          "illuminant": "not_applicable_reflectance",
          "observer": "not_applicable_reflectance",
          "unit": "spectral_reflectance",
          "numbering_order": "synthetic_reference_order",
          "expected_patch_count": 6,
          "expected_band_count": 4,
          "first_wavelength_nm": 430,
          "last_wavelength_nm": 650,
          "pairing_rgb_path": ")json" + camera_rgb.generic_string() + R"json("
        }
      }
    }
  })json");
  const int stale_rc =
      run_ccm_fit({"fixture", "--config", stale_config.string(),
                   "--illuminant-spd", illuminant.string(), "--out",
                   (root / "stale.json").string()});
  check(stale_rc == 1, "ccm-fit command: stale provenance config rejected");

  fs::remove_all(root);
}
