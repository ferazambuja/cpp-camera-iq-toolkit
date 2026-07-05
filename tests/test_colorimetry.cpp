#include "camera_iq/colorimetry.hpp"

#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <vector>

#include "harness.hpp"

using camera_iq::CameraRgbPatch;
using camera_iq::Lab;
using camera_iq::SpectralReference;
using camera_iq::SpectralReferencePatch;
using camera_iq::Xyz;
using camera_iq::fit_rgb_to_xyz_ccm;
using camera_iq::read_spectrum_csv_interpolated;
using camera_iq::render_reference_xyz;
using camera_iq::xyz_to_lab;
using test::check;
using test::check_near;

namespace {

SpectralReference flat_reference() {
  SpectralReference ref;
  ref.wavelengths_nm = {380, 390, 400, 410, 420, 430};
  SpectralReferencePatch white;
  white.id = "white";
  white.reflectance = {1, 1, 1, 1, 1, 1};
  SpectralReferencePatch half;
  half.id = "half";
  half.reflectance = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
  ref.patches = {white, half};
  return ref;
}

}  // namespace

void TESTS() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "camera_iq_colorimetry";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  {
    std::ofstream os(root / "illuminant.csv", std::ios::binary);
    os << "[plot 0],\n"
       << "Wavelength (nm),W/m2-um-sr\n"
       << "380,1\n"
       << "400,3\n"
       << "420,7\n"
       << "500,-1\n";
  }
  const auto interpolated =
      read_spectrum_csv_interpolated(root / "illuminant.csv",
                                     std::vector<double>{380, 390, 400, 410});
  check_near(interpolated[0], 1.0, 1e-12, "spectrum: exact first sample");
  check_near(interpolated[1], 2.0, 1e-12, "spectrum: interpolated sample");
  check_near(interpolated[3], 5.0, 1e-12, "spectrum: interpolated upper sample");

  {
    std::ofstream os(root / "bad_illuminant.csv", std::ios::binary);
    os << "Wavelength (nm),W/m2-um-sr\n"
       << "380,-1\n"
       << "400,3\n";
  }
  bool negative_threw = false;
  try {
    (void)read_spectrum_csv_interpolated(root / "bad_illuminant.csv",
                                         std::vector<double>{380, 390});
  } catch (const std::runtime_error&) {
    negative_threw = true;
  }
  check(negative_threw, "spectrum: negative target value rejected");

  const auto ref = flat_reference();
  const std::vector<double> illuminant(ref.wavelengths_nm.size(), 1.0);
  const auto rendered = render_reference_xyz(ref, illuminant);
  check(rendered.patch_xyz.size() == 2, "render: two patches");
  check_near(rendered.white_xyz.y, 100.0, 1e-9,
             "render: white normalized to Y=100");
  check_near(rendered.patch_xyz[0].x, rendered.white_xyz.x, 1e-9,
             "render: unit reflectance X equals white X");
  check_near(rendered.patch_xyz[0].z, rendered.white_xyz.z, 1e-9,
             "render: unit reflectance Z equals white Z");
  check_near(rendered.patch_xyz[1].y, 50.0, 1e-9,
             "render: half reflectance gives half Y");

  const Lab white_lab = xyz_to_lab(rendered.patch_xyz[0], rendered.white_xyz);
  check_near(white_lab.l, 100.0, 1e-9, "Lab: white L*");
  check_near(white_lab.a, 0.0, 1e-9, "Lab: white a*");
  check_near(white_lab.b, 0.0, 1e-9, "Lab: white b*");

  const std::vector<CameraRgbPatch> camera = {
      {1, 0, 0},
      {0, 1, 0},
      {0, 0, 1},
      {1, 1, 1},
      {0.5, 0.25, 0.75},
  };
  const std::vector<Xyz> target = {
      {2, 3, 5},
      {7, 11, 13},
      {17, 19, 23},
      {26, 33, 41},
      {15.5, 18.5, 23.0},
  };
  const auto fit = fit_rgb_to_xyz_ccm(camera, target, {95.047, 100, 108.883});
  check_near(fit.matrix[0][0], 2.0, 1e-9, "fit: X from R");
  check_near(fit.matrix[0][1], 7.0, 1e-9, "fit: X from G");
  check_near(fit.matrix[0][2], 17.0, 1e-9, "fit: X from B");
  check_near(fit.matrix[1][0], 3.0, 1e-9, "fit: Y from R");
  check_near(fit.matrix[1][1], 11.0, 1e-9, "fit: Y from G");
  check_near(fit.matrix[1][2], 19.0, 1e-9, "fit: Y from B");
  check_near(fit.matrix[2][0], 5.0, 1e-9, "fit: Z from R");
  check_near(fit.matrix[2][1], 13.0, 1e-9, "fit: Z from G");
  check_near(fit.matrix[2][2], 23.0, 1e-9, "fit: Z from B");
  check_near(fit.mean_delta_e_76, 0.0, 1e-9, "fit: zero mean dE76");
  check_near(fit.max_delta_e_76, 0.0, 1e-9, "fit: zero max dE76");

  bool threw = false;
  try {
    (void)fit_rgb_to_xyz_ccm({{1, 1, 1}, {2, 2, 2}, {3, 3, 3}},
                             {{1, 1, 1}, {2, 2, 2}, {3, 3, 3}},
                             {95.047, 100, 108.883});
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "fit: singular camera design rejected");

  std::filesystem::remove_all(root);
}
