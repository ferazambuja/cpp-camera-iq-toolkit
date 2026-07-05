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
using camera_iq::cross_validate_rgb_to_xyz_ccm;
using camera_iq::delta_e_2000;
using camera_iq::diagnose_dark_patches;
using camera_iq::evaluate_rgb_to_xyz_ccm;
using camera_iq::fit_rgb_to_xyz_ccm;
using camera_iq::read_spectrum_csv_interpolated;
using camera_iq::render_reference_xyz;
using camera_iq::select_reference_lightness;
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

  check_near(delta_e_2000({50.0, 2.6772, -79.7751},
                          {50.0, 0.0, -82.7485}),
             2.0425, 1e-4, "dE00: Sharma pair 1");
  check_near(delta_e_2000({50.0, 3.1571, -77.2803},
                          {50.0, 0.0, -82.7485}),
             2.8615, 1e-4, "dE00: Sharma pair 2");
  check_near(delta_e_2000({50.0, 2.8361, -74.0200},
                          {50.0, 0.0, -82.7485}),
             3.4412, 1e-4, "dE00: Sharma pair 3");
  check_near(delta_e_2000({50.0, -1.3802, -84.2814},
                          {50.0, 0.0, -82.7485}),
             1.0000, 1e-4, "dE00: Sharma pair 4");
  check_near(delta_e_2000({50.0, -1.1848, -84.8006},
                          {50.0, 0.0, -82.7485}),
             1.0000, 1e-4, "dE00: Sharma pair 5");
  check_near(delta_e_2000({50.0, -0.9009, -85.5211},
                          {50.0, 0.0, -82.7485}),
             1.0000, 1e-4, "dE00: Sharma pair 6");
  check_near(delta_e_2000({50.0, 0.0, 0.0}, {50.0, -1.0, 2.0}),
             2.3669, 1e-4, "dE00: neutral-chroma edge case");
  check_near(delta_e_2000({50.0, -1.0, 2.0}, {50.0, 0.0, 0.0}),
             2.3669, 1e-4, "dE00: symmetry for neutral-chroma edge case");
  check_near(delta_e_2000({50.0, 2.49, -0.001},
                          {50.0, -2.49, 0.0009}),
             7.1792, 1e-4, "dE00: Sharma hue-wrap pair 9");
  check_near(delta_e_2000({50.0, 2.49, -0.001},
                          {50.0, -2.49, 0.0011}),
             7.2195, 1e-4, "dE00: Sharma hue-wrap pair 11");

  const std::vector<CameraRgbPatch> camera = {
      {1, 0, 0},
      {0, 1, 0},
      {0, 0, 1},
      {1, 1, 1},
      {0.5, 0.25, 0.75},
      {0.25, 0.75, 0.5},
  };
  const std::vector<Xyz> target = {
      {2, 3, 5},
      {7, 11, 13},
      {17, 19, 23},
      {26, 33, 41},
      {15.5, 18.5, 23.0},
      {14.25, 18.5, 22.5},
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
  check_near(fit.mean_delta_e_2000, 0.0, 1e-9, "fit: zero mean dE00");
  check_near(fit.max_delta_e_2000, 0.0, 1e-9, "fit: zero max dE00");

  const auto cv =
      cross_validate_rgb_to_xyz_ccm(camera, target, {95.047, 100, 108.883}, 3);
  check(cv.fold_count == 3, "cv: requested fold count");
  check(cv.patch_count == camera.size(), "cv: evaluates every patch once");
  check_near(cv.mean_delta_e_76, 0.0, 1e-9, "cv: zero held-out mean dE76");
  check_near(cv.max_delta_e_2000, 0.0, 1e-9, "cv: zero held-out max dE00");

  auto nonlinear_target = target;
  nonlinear_target.back().z += 40.0;
  const auto nonlinear_fit =
      fit_rgb_to_xyz_ccm(camera, nonlinear_target, {95.047, 100, 108.883});
  const auto nonlinear_cv = cross_validate_rgb_to_xyz_ccm(
      camera, nonlinear_target, {95.047, 100, 108.883}, 3);
  check(nonlinear_fit.mean_delta_e_76 > 0.0,
        "cv: nonlinear fixture has training error");
  check(nonlinear_cv.mean_delta_e_76 > nonlinear_fit.mean_delta_e_76 + 1.0,
        "cv: held-out error is not the training metric");

  const std::vector<CameraRgbPatch> minimal_cv_camera = {
      {1, 0, 0},
      {0, 1, 0},
      {0, 0, 1},
      {1, 1, 1},
  };
  const std::vector<Xyz> minimal_cv_target = {
      {2, 3, 5},
      {7, 11, 13},
      {17, 19, 23},
      {26, 33, 41},
  };
  const auto leave_one_out_cv = cross_validate_rgb_to_xyz_ccm(
      minimal_cv_camera, minimal_cv_target, {95.047, 100, 108.883}, 5);
  check(leave_one_out_cv.fold_count == minimal_cv_camera.size(),
        "cv: small sample clamps to leave-one-out");
  check(leave_one_out_cv.patch_count == minimal_cv_camera.size(),
        "cv: leave-one-out evaluates every patch");
  check_near(leave_one_out_cv.max_delta_e_76, 0.0, 1e-9,
             "cv: leave-one-out exact dE76");

  const auto selection =
      select_reference_lightness({{2, 1, 1}, {50, 50, 50}, {60, 60, 60}},
                                 {100, 100, 100}, 25.0);
  check(selection.excluded_indices.size() == 1,
        "lightness selection: excludes one dark patch");
  check(selection.excluded_indices[0] == 0,
        "lightness selection: excluded index tracked");
  check(selection.kept_indices.size() == 2,
        "lightness selection: keeps non-dark patches");

  const std::array<std::array<double, 3>, 3> zero_matrix{};
  const auto dark_diag = diagnose_dark_patches(
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
      {{2, 1, 1}, {50, 50, 50}, {60, 60, 60}},
      {100, 100, 100}, zero_matrix, 25.0);
  check(dark_diag.patch_count == 1, "dark diagnostics: one L*<25 patch");
  check(dark_diag.worst_patch_index == 0,
        "dark diagnostics: worst patch index tracked");
  check(dark_diag.mean_delta_e_76 > 0.0,
        "dark diagnostics: reports nonzero dE76");
  check_near(dark_diag.mean_delta_e_76, dark_diag.max_delta_e_76, 1e-12,
             "dark diagnostics: single patch mean equals max");

  const auto two_dark_diag = diagnose_dark_patches(
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
      {{4, 4, 4}, {2, 1, 1}, {60, 60, 60}}, {100, 100, 100}, zero_matrix,
      25.0);
  check(two_dark_diag.patch_count == 2,
        "dark diagnostics: counts two dark patches");
  check(two_dark_diag.worst_patch_index == 1,
        "dark diagnostics: reports largest dark-patch dE76");

  const auto eval = evaluate_rgb_to_xyz_ccm(fit.matrix, camera, target,
                                            {95.047, 100, 108.883});
  check(eval.patch_count == camera.size(), "evaluation: patch count");
  check_near(eval.max_delta_e_2000, 0.0, 1e-9,
             "evaluation: exact matrix dE00");

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
