#include "camera_iq/dataset_config.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::dataset_file_label;
using camera_iq::dataset_root_label;
using camera_iq::read_dataset_config;
using camera_iq::resolve_dataset_root;
using test::check;

namespace {

void write_file(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream os(path, std::ios::binary);
  os << text;
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_dataset_config";
  fs::remove_all(root);
  fs::create_directories(root / "clrs");

  const fs::path config = root / "datasets.local.json";
  write_file(config, R"json({
    "_comment": "test config",
    "datasets": {
      "clrs589_project_camera": {
        "root": ")json" + (root / "clrs").string() + R"json(",
        "description": "CLRS fixture",
        "color_reference": {
          "id": "ccsg_2019_workbook",
          "role": "compatible_sg_spectral",
          "format": "camera_iq_spectral_csv",
          "path": "data/private/references/ccsg_2019_workbook/ccsg_2_FIXED_ref.csv",
          "source_xlsx": "data/private/references/ccsg_2019_workbook/ccsg.xlsx",
          "source_sheet": "ccsg_2_FIXED_ref",
          "selection_basis": "project_provenance_not_camera_date",
          "source": "compatible_2019_ccsg_workbook",
          "illuminant": "not_applicable_reflectance",
          "observer": "not_applicable_reflectance",
          "unit": "spectral_reflectance",
          "numbering_order": "A1_to_N10_row_major",
          "expected_patch_count": 140,
          "expected_band_count": 36,
          "first_wavelength_nm": 380,
          "last_wavelength_nm": 730,
          "min_reflectance": 0,
          "max_reflectance": 1,
          "pairing_rgb_path": "data/private/datasets/clrs589_project_camera/Images/ccsg_matlab.csv",
          "pairing_min_luminance_correlation": 0.90,
          "pairing_min_red_green_correlation": 0.80,
          "pairing_min_blue_green_correlation": 0.90
        }
      },
      "relative_fixture": {
        "root": "data/private/datasets/relative_fixture",
        "description": "relative path fixture",
        "color_reference": {
          "id": "sg_2016_i1pro_m0",
          "role": "representative_measured_sg",
          "format": "cgats_spectral",
          "path": "data/private/references/sg_2016_archive/color_management_color/Patch-Reader_chart_10Rx14C_2016-04-20_11h57_M0.txt",
          "selection_basis": "project_provenance_not_camera_date",
          "source": "2016_i1pro2_patchtool_m0",
          "illuminant": "D50",
          "observer": "2_degree_lab_weighting",
          "unit": "spectral_reflectance",
          "numbering_order": "A1_to_A10_then_B1_to_N10_patchreader_order",
          "expected_patch_count": 140,
          "expected_band_count": 36,
          "first_wavelength_nm": 380,
          "last_wavelength_nm": 730,
          "min_reflectance": 0,
          "max_reflectance": 1
        }
      }
    }
  })json");

  const auto datasets = read_dataset_config(config);
  check(datasets.size() == 2, "config: two datasets parsed");
  check(datasets.at("clrs589_project_camera").root == root / "clrs",
        "config: absolute root parsed");
  check(datasets.at("relative_fixture").root ==
            fs::path("data/private/datasets/relative_fixture"),
        "config: relative root preserved");
  check(datasets.at("clrs589_project_camera").color_reference.has_value(),
        "config: clrs reference parsed");
  check(datasets.at("clrs589_project_camera").color_reference->id ==
            "ccsg_2019_workbook",
        "config: clrs uses 2019 workbook reference");
  check(datasets.at("clrs589_project_camera").color_reference->format ==
            "camera_iq_spectral_csv",
        "config: clrs uses supported spectral csv");
  check(datasets.at("clrs589_project_camera")
            .color_reference->selection_basis ==
        "project_provenance_not_camera_date",
        "config: selection does not use camera dates");
  check(datasets.at("clrs589_project_camera").color_reference->unit ==
            "spectral_reflectance",
        "config: reference unit parsed");
  check(datasets.at("clrs589_project_camera")
            .color_reference->expected_patch_count == 140,
        "config: expected patch count parsed");
  check(datasets.at("clrs589_project_camera")
            .color_reference->first_wavelength_nm == 380.0,
        "config: wavelength constraint parsed");
  check(datasets.at("clrs589_project_camera")
            .color_reference->pairing_rgb_path ==
        fs::path("data/private/datasets/clrs589_project_camera/Images/"
                 "ccsg_matlab.csv"),
        "config: pairing RGB path parsed");
  check(datasets.at("clrs589_project_camera")
            .color_reference->pairing_min_red_green_correlation == 0.80,
        "config: pairing threshold parsed");
  check(datasets.at("relative_fixture").color_reference.has_value(),
        "config: old reference parsed");
  check(datasets.at("relative_fixture").color_reference->id ==
            "sg_2016_i1pro_m0",
        "config: old archive can use 2016 reference");
  check(datasets.at("relative_fixture").color_reference->numbering_order ==
            "A1_to_A10_then_B1_to_N10_patchreader_order",
        "config: old reference order parsed");

  const auto by_id = resolve_dataset_root("clrs589_project_camera", config);
  check(by_id.has_value(), "resolve: dataset id");
  if (by_id) {
    check(by_id->id == "clrs589_project_camera", "resolve: id retained");
    check(by_id->root == root / "clrs", "resolve: root from config");
    check(by_id->label == "dataset:clrs589_project_camera",
          "resolve: redacted dataset label");
    check(by_id->from_config, "resolve: marked from config");
  }

  const auto by_path = resolve_dataset_root((root / "clrs").string(), config);
  check(by_path.has_value(), "resolve: direct path");
  if (by_path) {
    check(!by_path->from_config, "resolve: direct path not config");
    check(by_path->label == (root / "clrs").string(),
          "resolve: direct path label unchanged");
  }

  check(!resolve_dataset_root("missing_dataset", config).has_value(),
        "resolve: missing id is empty");

  check(dataset_root_label("clrs589_project_camera") ==
            "dataset:clrs589_project_camera",
        "label: dataset root");
  check(dataset_file_label("clrs589_project_camera",
                           fs::path("Images/CCSG/file.RAF")) ==
            "dataset:clrs589_project_camera/Images/CCSG/file.RAF",
        "label: dataset file");

  fs::remove_all(root);
}
