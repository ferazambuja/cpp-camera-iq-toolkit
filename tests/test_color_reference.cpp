#include "camera_iq/color_reference.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::read_spectral_reference_csv;
using camera_iq::read_spectral_reference_cgats;
using camera_iq::read_camera_rgb_csv;
using camera_iq::evaluate_reference_pairing;
using camera_iq::validate_spectral_reference;
using camera_iq::SpectralReferencePairingThresholds;
using camera_iq::SpectralReferenceProvenance;
using camera_iq::SpectralReferenceValidation;
using test::check;

namespace {

void write_file(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream os(path, std::ios::binary);
  os << text;
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_color_reference";
  fs::remove_all(root);

  const fs::path good = root / "ccsg_ref.csv";
  write_file(good,
             "patch_id,380,390,400\n"
             "A1,0.10,0.20,0.30\n"
             "B1,0.40,0.50,0.60\n");

  SpectralReferenceProvenance provenance;
  provenance.source = "compatible_2019_ccsg_workbook";
  provenance.illuminant = "not_applicable_reflectance";
  provenance.observer = "not_applicable_reflectance";
  provenance.unit = "spectral_reflectance";
  provenance.numbering_order =
      "workbook_number_major_A1_B1_to_N1_then_A2_to_N10";
  const auto ref = read_spectral_reference_csv(good, "ccsg_fixture",
                                               provenance);
  check(ref.source_label == "ccsg_fixture", "reference: source label");
  check(ref.provenance.source == "compatible_2019_ccsg_workbook",
        "reference: provenance source retained");
  check(ref.provenance.unit == "spectral_reflectance",
        "reference: provenance unit retained");
  check(ref.wavelengths_nm.size() == 3, "reference: wavelength count");
  check(ref.wavelengths_nm[0] == 380.0 && ref.wavelengths_nm[2] == 400.0,
        "reference: wavelengths parsed");
  check(ref.patches.size() == 2, "reference: patch count");
  check(ref.patches[0].id == "A1" && ref.patches[1].id == "B1",
        "reference: patch ids parsed");
  check(ref.patches[1].reflectance[2] == 0.60,
        "reference: reflectance parsed");

  SpectralReferenceValidation validation;
  validation.expected_patch_count = 2;
  validation.expected_band_count = 3;
  validation.first_wavelength_nm = 380.0;
  validation.last_wavelength_nm = 400.0;
  validation.min_reflectance = 0.0;
  validation.max_reflectance = 1.0;
  validate_spectral_reference(ref, validation);

  bool threw = false;
  write_file(root / "bad_axis.csv",
             "patch_id,380,380,400\n"
             "A1,0.1,0.2,0.3\n");
  try {
    (void)read_spectral_reference_csv(root / "bad_axis.csv", "bad_axis");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "reference: duplicate wavelength rejected");

  threw = false;
  write_file(root / "bad_row.csv",
             "patch_id,380,390,400\n"
             "A1,0.1,0.2\n");
  try {
    (void)read_spectral_reference_csv(root / "bad_row.csv", "bad_row");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "reference: wrong row width rejected");

  threw = false;
  write_file(root / "bad_range.csv",
             "patch_id,380,390,400\n"
             "A1,0.1,1.2,0.3\n");
  try {
    const auto bad_ref =
        read_spectral_reference_csv(root / "bad_range.csv", "bad_range");
    validate_spectral_reference(bad_ref, validation);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "reference: out-of-range reflectance rejected");

  threw = false;
  try {
    SpectralReferenceValidation bad_count = validation;
    bad_count.expected_patch_count = 140;
    validate_spectral_reference(ref, bad_count);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "reference: wrong expected patch count rejected");

  const fs::path cgats = root / "sg_2016.txt";
  write_file(cgats,
             "CGATS.17\n"
             "MEASUREMENT_SOURCE \"Illumination=D50 ObserverAngle=2 WhiteBase=Abs Filter=no\"\n"
             "ILLUMINATION_NAME \"D50\"\n"
             "OBSERVER_ANGLE \"2\"\n"
             "NUMBER_OF_FIELDS 8\n"
             "BEGIN_DATA_FORMAT\n"
             "SAMPLE_ID SAMPLE_NAME LAB_L LAB_A LAB_B SPECTRAL_NM380 SPECTRAL_NM390 SPECTRAL_NM400\n"
             "END_DATA_FORMAT\n"
             "NUMBER_OF_SETS 2\n"
             "BEGIN_DATA\n"
             "1 A1 94.0 0.0 0.0 0.11 0.12 0.13\n"
             "2 A2 50.0 0.0 0.0 0.21 0.22 0.23\n"
             "END_DATA\n");
  const auto cgats_ref = read_spectral_reference_cgats(cgats, "sg_2016");
  check(cgats_ref.source_label == "sg_2016", "cgats: source label");
  check(cgats_ref.provenance.illuminant == "D50",
        "cgats: illuminant metadata parsed");
  check(cgats_ref.provenance.observer == "2",
        "cgats: observer metadata parsed");
  check(cgats_ref.provenance.unit == "spectral_reflectance",
        "cgats: unit metadata set");
  check(cgats_ref.wavelengths_nm.size() == 3, "cgats: wavelength count");
  check(cgats_ref.patches.size() == 2, "cgats: patch count");
  check(cgats_ref.patches[0].id == "A1", "cgats: sample name used");
  check(cgats_ref.patches[1].reflectance[2] == 0.23,
        "cgats: spectral values parsed");

  const fs::path pair_ref = root / "pair_ref.csv";
  write_file(pair_ref,
             "patch_id,430,500,550,620\n"
             "A1,0.8,0.8,0.8,0.8\n"
             "B1,0.1,0.2,0.2,0.9\n"
             "C1,0.9,0.2,0.2,0.1\n"
             "D1,0.1,0.9,0.9,0.1\n");
  const fs::path pair_camera = root / "pair_camera.csv";
  write_file(pair_camera,
             "0.8,0.8,0.8\n"
             "0.9,0.2,0.1\n"
             "0.1,0.2,0.9\n"
             "0.1,0.9,0.1\n");
  const auto pair_ref_data =
      read_spectral_reference_csv(pair_ref, "pair_ref");
  const auto pair_camera_data = read_camera_rgb_csv(pair_camera);
  SpectralReferencePairingThresholds thresholds;
  thresholds.min_luminance_correlation = 0.95;
  thresholds.min_red_green_correlation = 0.95;
  thresholds.min_blue_green_correlation = 0.95;
  const auto pairing =
      evaluate_reference_pairing(pair_ref_data, pair_camera_data, thresholds);
  check(pairing.patch_count == 4, "pairing: patch count");
  check(pairing.passes, "pairing: aligned camera/reference order passes");

  const fs::path shifted_camera = root / "pair_camera_shifted.csv";
  write_file(shifted_camera,
             "0.9,0.2,0.1\n"
             "0.1,0.2,0.9\n"
             "0.1,0.9,0.1\n"
             "0.8,0.8,0.8\n");
  const auto shifted = evaluate_reference_pairing(
      pair_ref_data, read_camera_rgb_csv(shifted_camera), thresholds);
  check(!shifted.passes, "pairing: shifted camera/reference order rejected");

  fs::remove_all(root);
}
