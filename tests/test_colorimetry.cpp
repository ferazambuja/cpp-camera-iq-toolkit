#include "camera_iq/colorimetry.hpp"

#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::CameraRgbPatch;
using camera_iq::Cie94Application;
using camera_iq::Ipt;
using camera_iq::Lab;
using camera_iq::Oklab;
using camera_iq::Oklch;
using camera_iq::SpectralReference;
using camera_iq::SpectralReferencePatch;
using camera_iq::Xyz;
using camera_iq::cross_validate_rgb_to_xyz_ccm;
using camera_iq::delta_e_76;
using camera_iq::delta_e_2000;
using camera_iq::delta_e_94;
using camera_iq::delta_e_94_geometric_mean_chroma;
using camera_iq::diagnose_dark_patches;
using camera_iq::evaluate_rgb_to_xyz_ccm;
using camera_iq::fit_rgb_to_xyz_ccm;
using camera_iq::lab_to_xyz;
using camera_iq::oklab_to_oklch;
using camera_iq::oklab_to_xyz_d65;
using camera_iq::oklch_to_oklab;
using camera_iq::read_spectrum_csv_interpolated;
using camera_iq::render_reference_xyz;
using camera_iq::select_reference_lightness;
using camera_iq::xyz_to_lab;
using camera_iq::xyz_d65_to_ipt;
using camera_iq::xyz_d65_to_oklab;
using camera_iq::delta_e_ok;
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

struct HistoricalCie94Pair {
  Lab reference;
  Lab sample;
  double printed = 0;
};

std::vector<HistoricalCie94Pair> read_historical_cie94_fixture() {
  const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                    "docs/data/cie94_historical_24patch.csv";
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("dE94: cannot open historical 24-patch fixture");
  }
  std::string line;
  std::getline(input, line);
  if (line != "name,reference_L,reference_a,reference_b,image_L,image_a,image_b,printed_delta_e_94") {
    throw std::runtime_error("dE94: unexpected historical fixture header");
  }
  std::vector<HistoricalCie94Pair> rows;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    std::stringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(field);
    if (fields.size() != 8) {
      throw std::runtime_error("dE94: malformed historical fixture row");
    }
    rows.push_back({{std::stod(fields[1]), std::stod(fields[2]),
                     std::stod(fields[3])},
                    {std::stod(fields[4]), std::stod(fields[5]),
                     std::stod(fields[6])},
                    std::stod(fields[7])});
  }
  return rows;
}

struct HistoricalCie94Summary {
  double mean = 0;
  double best_22 = 0;
  double worst_2 = 0;
};

HistoricalCie94Summary summarize_cie94(std::vector<double> values) {
  if (values.size() != 24) {
    throw std::runtime_error("dE94: historical summary requires 24 patches");
  }
  std::sort(values.begin(), values.end());
  double total = 0;
  double best = 0;
  double worst = 0;
  for (std::size_t i = 0; i < values.size(); ++i) {
    total += values[i];
    if (i < 22) best += values[i];
    if (i >= 22) worst += values[i];
  }
  return {total / 24.0, best / 22.0, worst / 2.0};
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

  {
    std::ofstream os(root / "malformed_illuminant.csv", std::ios::binary);
    os << "Wavelength (nm),W/m2-um-sr\n"
       << "380,1\n"
       << "400,not-a-number\n"
       << "420,3\n";
  }
  bool malformed_row_threw = false;
  try {
    (void)read_spectrum_csv_interpolated(
        root / "malformed_illuminant.csv",
        std::vector<double>{380, 390, 400, 410, 420});
  } catch (const std::runtime_error& error) {
    malformed_row_threw =
        std::string(error.what()).find("malformed numeric row") !=
        std::string::npos;
  }
  check(malformed_row_threw,
        "spectrum: malformed row after numeric data begins is rejected");

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

  const double large_delta =
      delta_e_76({1e308, 0.0, 0.0}, {0.0, 1e308, 0.0});
  check(std::isfinite(large_delta) &&
            std::abs(large_delta / 1e308 - std::sqrt(2.0)) < 1e-15,
        "dE76: representable large distance remains finite");
  bool nonfinite_lab_threw = false;
  try {
    (void)delta_e_76(
        {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
        {0.0, 0.0, 0.0});
  } catch (const std::runtime_error&) {
    nonfinite_lab_threw = true;
  }
  check(nonfinite_lab_threw, "dE76: non-finite Lab input is refused");

  const Xyz d65{0.9504559270516717, 1.0, 1.0890577507598784};
  // Literal references generated by the independently written RIT MCSL
  // XYZ2IPT.m implementation pinned at 50bd4d6381899d637ad228044e050f82d32a946c.
  // These fail if either IPT matrix, the signed 0.43 response, or XYZ scaling
  // changes.
  const Ipt ipt_white = xyz_d65_to_ipt(d65);
  check_near(ipt_white.i, 1.00000467798545, 1e-13,
             "IPT: D65 white intensity reference");
  check_near(ipt_white.p, 0.000116529059649379, 1e-13,
             "IPT: D65 white protan reference");
  check_near(ipt_white.t, -0.000108572629236336, 1e-13,
             "IPT: D65 white tritan reference");

  const Ipt ipt_p3_yellow = xyz_d65_to_ipt(
      {0.7522386418173092, 0.920713085906255,
       0.04511338185890257});
  check_near(ipt_p3_yellow.i, 0.822399058775996, 1e-13,
             "IPT: Display-P3 yellow intensity reference");
  check_near(ipt_p3_yellow.p, -0.157870695599737, 1e-13,
             "IPT: Display-P3 yellow protan reference");
  check_near(ipt_p3_yellow.t, 0.831580902060753, 1e-13,
             "IPT: Display-P3 yellow tritan reference");

  bool ipt_overflow_threw = false;
  try {
    (void)xyz_d65_to_ipt({std::numeric_limits<double>::max(),
                          std::numeric_limits<double>::max(),
                          std::numeric_limits<double>::max()});
  } catch (const std::runtime_error&) {
    ipt_overflow_threw = true;
  }
  check(ipt_overflow_threw,
        "IPT: finite XYZ that overflows the transform is rejected");

  // External literals from the W3C CSS Color 4 sample conversion and
  // Display-P3-yellow example, Candidate Recommendation Draft 2026-07-28.
  // These references are independent of the C++ implementation below.
  const Oklab ok_red = xyz_d65_to_oklab(
      {0.41239079926595951, 0.21263900587151036,
       0.019330818715591849});
  check_near(ok_red.l, 0.62795536392143125, 2e-15,
             "OkLab: sRGB red L reference");
  check_near(ok_red.a, 0.22486306842627424, 2e-15,
             "OkLab: sRGB red a reference");
  check_near(ok_red.b, 0.12584627733058495, 2e-15,
             "OkLab: sRGB red b reference");

  const Xyz p3_yellow_xyz{0.7522386418173092, 0.920713085906255,
                          0.04511338185890257};
  const Oklch ok_yellow = oklab_to_oklch(xyz_d65_to_oklab(p3_yellow_xyz));
  check_near(ok_yellow.l, 0.96476, 5e-6,
             "OkLCh: W3C Display-P3 yellow L reference");
  check_near(ok_yellow.c, 0.24503, 5e-6,
             "OkLCh: W3C Display-P3 yellow C reference");
  check_near(ok_yellow.h_degrees, 110.23, 5e-3,
             "OkLCh: W3C Display-P3 yellow hue reference");
  check(ok_yellow.hue_defined,
        "OkLCh: chromatic Display-P3 yellow has a defined hue");

  const Xyz ok_red_round_trip = oklab_to_xyz_d65(ok_red);
  check_near(ok_red_round_trip.x, 0.41239079926595951, 2e-15,
             "OkLab: sRGB red round-trip X");
  check_near(ok_red_round_trip.y, 0.21263900587151036, 2e-15,
             "OkLab: sRGB red round-trip Y");
  check_near(ok_red_round_trip.z, 0.019330818715591849, 2e-15,
             "OkLab: sRGB red round-trip Z");

  const Oklch wrapped{ok_yellow.l, ok_yellow.c,
                      ok_yellow.h_degrees + 360.0, true};
  const Oklab wrapped_cartesian = oklch_to_oklab(wrapped);
  check_near(wrapped_cartesian.a, xyz_d65_to_oklab(p3_yellow_xyz).a, 2e-15,
             "OkLCh: hue wrapping preserves a");
  check_near(wrapped_cartesian.b, xyz_d65_to_oklab(p3_yellow_xyz).b, 2e-15,
             "OkLCh: hue wrapping preserves b");

  const Oklch neutral = oklab_to_oklch({0.5, 0.0, 0.0});
  check(!neutral.hue_defined, "OkLCh: neutral hue is explicitly undefined");
  check_near(neutral.c, 0.0, 0.0, "OkLCh: neutral chroma is zero");
  const Oklab powerless_source{0.5, 0.000003, 0.0};
  const Oklch powerless = oklab_to_oklch(powerless_source);
  check(!powerless.hue_defined,
        "OkLCh: chroma below the powerless threshold has missing hue");
  const Oklab powerless_round_trip = oklch_to_oklab(powerless);
  check_near(powerless_round_trip.l, powerless_source.l, 0.0,
             "OkLCh inverse: missing hue preserves lightness");
  check_near(powerless_round_trip.a, 0.0, 0.0,
             "OkLCh inverse: missing hue converts to zero a");
  check_near(powerless_round_trip.b, 0.0, 0.0,
             "OkLCh inverse: missing hue converts to zero b");
  check(delta_e_ok(powerless_source, powerless_round_trip) <= 0.000004,
        "OkLCh inverse: powerless-hue chroma loss is bounded by the threshold");
  check_near(delta_e_ok({0.5, 0.1, -0.2}, {0.6, 0.1, -0.2}), 0.1,
             1e-15, "deltaEOK: Euclidean lightness difference");

  bool oklab_overflow_threw = false;
  try {
    (void)xyz_d65_to_oklab({std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::max()});
  } catch (const std::runtime_error&) {
    oklab_overflow_threw = true;
  }
  check(oklab_overflow_threw,
        "OkLab: finite XYZ that overflows the transform is rejected");

  const Xyz lab_white_xyz = lab_to_xyz({100.0, 0.0, 0.0}, d65);
  check_near(lab_white_xyz.x, d65.x, 1e-12, "Lab inverse: white X");
  check_near(lab_white_xyz.y, d65.y, 1e-12, "Lab inverse: white Y");
  check_near(lab_white_xyz.z, d65.z, 1e-12, "Lab inverse: white Z");

  const Xyz lab_black_xyz = lab_to_xyz({0.0, 0.0, 0.0}, d65);
  check_near(lab_black_xyz.x, 0.0, 1e-12, "Lab inverse: black X");
  check_near(lab_black_xyz.y, 0.0, 1e-12, "Lab inverse: black Y");
  check_near(lab_black_xyz.z, 0.0, 1e-12, "Lab inverse: black Z");

  const Xyz srgb_red_xyz =
      lab_to_xyz({53.237115595429373, 80.090113523103796,
                  67.203263511722142},
                 d65);
  check_near(srgb_red_xyz.x, 0.41239079926595951, 1e-12,
             "Lab inverse: sRGB red X reference");
  check_near(srgb_red_xyz.y, 0.21263900587151036, 1e-12,
             "Lab inverse: sRGB red Y reference");
  check_near(srgb_red_xyz.z, 0.019330818715591849, 1e-12,
             "Lab inverse: sRGB red Z reference");

  const Xyz below_break =
      lab_to_xyz({7.999883999999998, 0.0, 0.0}, {1.0, 1.0, 1.0});
  check_near(below_break.y, 0.008856323260486285, 1e-15,
             "Lab inverse: linear branch below delta");
  const Xyz above_break =
      lab_to_xyz({8.000115999999998, 0.0, 0.0}, {1.0, 1.0, 1.0});
  check_near(above_break.y, 0.008856580098205667, 1e-15,
             "Lab inverse: cubic branch above delta");

  const Xyz d65_100{95.04559270516717, 100.0, 108.90577507598784};
  const Xyz scaled_sample{41.23907992659595, 21.263900587151036,
                          1.9330818715591849};
  const Lab scaled_red_lab = xyz_to_lab(scaled_sample, d65_100);
  const Xyz scaled_round_trip = lab_to_xyz(scaled_red_lab, d65_100);
  check_near(scaled_round_trip.x, scaled_sample.x, 1e-12,
             "Lab scale: Y=100 round-trip X");
  check_near(scaled_round_trip.y, scaled_sample.y, 1e-12,
             "Lab scale: Y=100 round-trip Y");
  check_near(scaled_round_trip.z, scaled_sample.z, 1e-12,
             "Lab scale: Y=100 round-trip Z");

  bool nonfinite_xyz_threw = false;
  try {
    (void)xyz_to_lab({std::numeric_limits<double>::quiet_NaN(), 0.2, 0.3},
                     d65);
  } catch (const std::runtime_error&) {
    nonfinite_xyz_threw = true;
  }
  check(nonfinite_xyz_threw, "Lab: non-finite XYZ rejected");

  const Lab extended_negative_xyz = xyz_to_lab({-0.01, 0.1, 0.1}, d65);
  check(std::isfinite(extended_negative_xyz.l) &&
            std::isfinite(extended_negative_xyz.a) &&
            std::isfinite(extended_negative_xyz.b),
        "Lab: finite negative XYZ uses the declared extended domain");

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

  const Lab historical_blue_reference{26.63, 18.17, -50.53};
  const Lab historical_blue_sample{26.74, 5.35, -49.87};
  check_near(delta_e_94(historical_blue_reference, historical_blue_sample,
                        Cie94Application::GraphicArts),
             6.913195109283125, 1e-12,
             "dE94: historical graphic-arts reference order");
  check_near(delta_e_94(historical_blue_sample, historical_blue_reference,
                        Cie94Application::GraphicArts),
             7.1256319062258315, 1e-12,
             "dE94: swapped reference order remains explicitly asymmetric");
  check_near(delta_e_94(historical_blue_reference, historical_blue_sample,
                        Cie94Application::Textiles),
             7.113076454347559, 1e-12,
             "dE94: textile weighting is a separate declared application");
  check_near(delta_e_94_geometric_mean_chroma(
                 historical_blue_reference, historical_blue_sample,
                 Cie94Application::GraphicArts),
             7.0195983505209476, 1e-12,
             "dE94: geometric-mean-chroma variant is separately named");
  check_near(delta_e_94_geometric_mean_chroma(
                 historical_blue_sample, historical_blue_reference,
                 Cie94Application::GraphicArts),
             7.0195983505209476, 1e-12,
             "dE94: geometric-mean-chroma variant is symmetric");

  const auto historical_pairs = read_historical_cie94_fixture();
  check(historical_pairs.size() == 24,
        "dE94: historical fixture retains all 24 patches");
  std::vector<double> chart_reference_values;
  std::vector<double> image_reference_values;
  std::vector<double> geometric_mean_values;
  double max_printed_residual = 0;
  for (const auto& pair : historical_pairs) {
    chart_reference_values.push_back(delta_e_94(
        pair.reference, pair.sample, Cie94Application::GraphicArts));
    image_reference_values.push_back(delta_e_94(
        pair.sample, pair.reference, Cie94Application::GraphicArts));
    const double geometric = delta_e_94_geometric_mean_chroma(
        pair.reference, pair.sample, Cie94Application::GraphicArts);
    geometric_mean_values.push_back(geometric);
    max_printed_residual =
        std::max(max_printed_residual, std::abs(geometric - pair.printed));
  }
  const auto chart_reference = summarize_cie94(chart_reference_values);
  const auto image_reference = summarize_cie94(image_reference_values);
  const auto geometric_mean = summarize_cie94(geometric_mean_values);
  check_near(chart_reference.mean, 3.094811, 1e-6,
             "dE94: 24-patch chart-reference mean");
  check_near(chart_reference.best_22, 2.750829, 1e-6,
             "dE94: 24-patch chart-reference best 22");
  check_near(chart_reference.worst_2, 6.878626, 1e-6,
             "dE94: 24-patch chart-reference worst 2");
  check_near(image_reference.mean, 3.101669, 1e-6,
             "dE94: 24-patch image-reference mean");
  check_near(image_reference.best_22, 2.751039, 1e-6,
             "dE94: 24-patch image-reference best 22");
  check_near(image_reference.worst_2, 6.958594, 1e-6,
             "dE94: 24-patch image-reference worst 2");
  check_near(geometric_mean.mean, 3.098152, 1e-6,
             "dE94: 24-patch historical-variant mean");
  check_near(geometric_mean.best_22, 2.750835, 1e-6,
             "dE94: 24-patch historical-variant best 22");
  check_near(geometric_mean.worst_2, 6.918646, 1e-6,
             "dE94: 24-patch historical-variant worst 2");
  check(max_printed_residual < 0.015,
        "dE94: historical variant matches every printed patch within rounding");

  bool cie94_nonfinite_threw = false;
  try {
    (void)delta_e_94(
        {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0},
        historical_blue_sample, Cie94Application::GraphicArts);
  } catch (const std::runtime_error&) {
    cie94_nonfinite_threw = true;
  }
  check(cie94_nonfinite_threw, "dE94: non-finite Lab is rejected");

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
