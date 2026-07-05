#include "camera_iq/color_reference.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::read_spectral_reference_csv;
using camera_iq::read_spectral_reference_cgats;
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

  const auto ref = read_spectral_reference_csv(good, "ccsg_fixture");
  check(ref.source_label == "ccsg_fixture", "reference: source label");
  check(ref.wavelengths_nm.size() == 3, "reference: wavelength count");
  check(ref.wavelengths_nm[0] == 380.0 && ref.wavelengths_nm[2] == 400.0,
        "reference: wavelengths parsed");
  check(ref.patches.size() == 2, "reference: patch count");
  check(ref.patches[0].id == "A1" && ref.patches[1].id == "B1",
        "reference: patch ids parsed");
  check(ref.patches[1].reflectance[2] == 0.60,
        "reference: reflectance parsed");

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

  const fs::path cgats = root / "sg_2016.txt";
  write_file(cgats,
             "CGATS.17\n"
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
  check(cgats_ref.wavelengths_nm.size() == 3, "cgats: wavelength count");
  check(cgats_ref.patches.size() == 2, "cgats: patch count");
  check(cgats_ref.patches[0].id == "A1", "cgats: sample name used");
  check(cgats_ref.patches[1].reflectance[2] == 0.23,
        "cgats: spectral values parsed");

  fs::remove_all(root);
}
