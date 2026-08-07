#include "camera_iq/spectral_reference_audit.hpp"

#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "camera_iq/colorimetry.hpp"
#include "harness.hpp"

using camera_iq::SpectralReference;
using camera_iq::SpectralReferencePatch;
using camera_iq::SpectroCmfTable;
using camera_iq::audit_spectral_reference_colorimetry;
using camera_iq::audit_spectral_reference_repeat;
using camera_iq::read_spectral_reference_csv;
using camera_iq::render_reference_xyz;
using camera_iq::read_spectral_reference_cgats;
using camera_iq::read_spectro_cmf_csv;
using camera_iq::read_spectrum_csv_interpolated;
using camera_iq::xyz_to_lab;
using test::check;
using test::check_near;

namespace {

template <typename Operation>
bool throws_with(Operation operation, const std::string& needle) {
  try {
    operation();
  } catch (const std::runtime_error& error) {
    return std::string(error.what()).find(needle) != std::string::npos;
  }
  return false;
}

}  // namespace

void TESTS() {
  SpectralReference reference;
  reference.wavelengths_nm = {400.0, 500.0, 600.0};
  SpectralReferencePatch white;
  white.id = "white";
  white.sample_id = "1";
  white.reflectance = {1.0, 1.0, 1.0};
  SpectralReferencePatch gray;
  gray.id = "gray";
  gray.sample_id = "2";
  gray.reflectance = {0.5, 0.5, 0.5};
  reference.patches = {white, gray};
  reference.cgats_schema = camera_iq::CgatsSchemaDiagnostics{};
  reference.cgats_schema->observer_declarations = {
      "OBSERVER_ANGLE=2", "WEIGHTING_FUNCTION=OBSERVER,10 degree"};
  reference.cgats_schema->observer_declarations_conflict = true;

  SpectroCmfTable observer;
  observer.wavelength_nm = reference.wavelengths_nm;
  for (auto& channel : observer.xyz_bar) channel = {1.0, 1.0, 1.0};
  const std::vector<double> illuminant = {1.0, 1.0, 1.0};
  const auto rendered = render_reference_xyz(reference, illuminant, observer);
  check(rendered.white_xyz.x == 100.0 && rendered.white_xyz.y == 100.0 &&
            rendered.white_xyz.z == 100.0,
        "reference audit: explicit observer renders its own white at Y=100");
  for (std::size_t index = 0; index < reference.patches.size(); ++index) {
    const auto lab = xyz_to_lab(rendered.patch_xyz[index], rendered.white_xyz);
    reference.patches[index].declared_lab = {lab.l, lab.a, lab.b};
    reference.patches[index].declared_xyz = {
        rendered.patch_xyz[index].x, rendered.patch_xyz[index].y,
        rendered.patch_xyz[index].z};
  }

  const auto audit = audit_spectral_reference_colorimetry(
      reference, illuminant, observer, "D65", "CIE_1964_10_degree");
  check(audit.illuminant == "D65" &&
            audit.observer == "CIE_1964_10_degree" &&
            audit.observer_metadata_conflict,
        "reference audit: caller choice and source conflict are both retained");
  check(audit.lab_patch_count == 2 && audit.xyz_patch_count == 2,
        "reference audit: every declared colorimetric record is compared");
  check_near(audit.mean_delta_e_76, 0.0, 1e-12,
             "reference audit: matching Lab has zero Delta E 76");
  check_near(audit.max_xyz_relative_l2, 0.0, 1e-12,
             "reference audit: matching XYZ has zero relative residual");

  check(throws_with(
            [&] {
              auto wrong = observer;
              wrong.wavelength_nm[1] = 510.0;
              (void)render_reference_xyz(reference, illuminant, wrong);
            },
            "observer and reference wavelength axes differ"),
        "reference audit: implicit observer interpolation is refused");

  const std::filesystem::path source_root = CAMERA_IQ_SOURCE_DIR;
  const auto read_text = [](const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
  };
  const auto spectra_shop = read_spectral_reference_cgats(
      source_root / "data/samples/spectral_2017/CC4_CGATS.txt",
      "spectrashop");
  const auto d65 = read_spectrum_csv_interpolated(
      source_root / "data/cie_d65.csv", spectra_shop.wavelengths_nm);
  const auto observer10 = read_spectro_cmf_csv(
      read_text(source_root / "data/cie1964_10deg_cmf.csv"));
  const auto observer2 = read_spectro_cmf_csv(
      read_text(source_root / "data/cie1931_2deg_cmf.csv"));
  const auto matching = audit_spectral_reference_colorimetry(
      spectra_shop, d65, observer10, "D65", "CIE_1964_10_degree");
  const auto alternative = audit_spectral_reference_colorimetry(
      spectra_shop, d65, observer2, "D65", "CIE_1931_2_degree");
  CAMERA_IQ_DOC_EVIDENCE(
      spectral_reference_observer_oracle,
      check_near(matching.mean_delta_e_76, 0.0118573441, 5e-7,
                 "reference audit: D65/10-degree reproduces SpectraShop Lab"));
  CAMERA_IQ_DOC_EVIDENCE(
      spectral_reference_observer_oracle,
      check_near(matching.max_delta_e_76, 0.0412437388, 5e-7,
                 "reference audit: D65/10-degree maximum is pinned"));
  CAMERA_IQ_DOC_EVIDENCE(
      spectral_reference_observer_oracle,
      check_near(alternative.mean_delta_e_76, 3.9091857607, 5e-7,
                 "reference audit: explicit 2-degree alternative is discriminating"));
  CAMERA_IQ_DOC_EVIDENCE(
      spectral_reference_observer_oracle,
      check(alternative.mean_delta_e_76 > 300.0 * matching.mean_delta_e_76,
            "reference audit: commercial oracle discriminates observer choice"));

  const auto babel = read_spectral_reference_cgats(
      source_root / "data/samples/spectral_2017/CC4_CGATS_M0.txt",
      "babelcolor");
  const auto babel_audit = audit_spectral_reference_colorimetry(
      babel, d65, observer2, "D65", "CIE_1931_2_degree");
  check(babel_audit.xyz_patch_count == 24 &&
            babel_audit.mean_xyz_relative_l2 < 0.001,
        "reference audit: BabelColor XYZ is a separate software oracle");

  const auto layout = read_spectral_reference_cgats(
      source_root / "data/samples/spectral_2017/CC4_4_M0.txt", "layout");
  const auto interchange =
      camera_iq::compare_spectral_reference_interchange(babel, layout);
  check(interchange.stable_id_set_matches &&
            interchange.spectra_match_by_stable_id &&
            interchange.exact_spectral_multiset_identity &&
            !interchange.layout_labels_match_by_stable_id,
        "reference audit: exact spectra survive vendor layout relabeling");

  const auto repeat_first = read_spectral_reference_csv(
      source_root /
      "data/samples/spectral_2017/colorchecker_measurement_01.csv",
      "measurement_01");
  const auto repeat_second = read_spectral_reference_csv(
      source_root /
      "data/samples/spectral_2017/colorchecker_measurement_02.csv",
      "measurement_02");
  const auto d55 = read_spectrum_csv_interpolated(
      source_root / "data/cie_d55.csv", repeat_first.wavelengths_nm);
  const auto repeat_audit = audit_spectral_reference_repeat(
      repeat_first, repeat_second, d55, observer2, "D55",
      "CIE_1931_2_degree");
  check(repeat_audit.patch_count == 24,
        "reference repeat: all candidate paired patches are retained");
  check_near(repeat_audit.mean_reflectance_rms, 0.00458179055, 1e-10,
             "reference repeat: mean reflectance RMS is pinned");
  check_near(repeat_audit.max_reflectance_rms, 0.00852470201, 1e-10,
             "reference repeat: maximum reflectance RMS is pinned");
  check_near(repeat_audit.mean_delta_e_76, 0.851467674136, 1e-9,
             "reference repeat: D55/2-degree mean Delta E 76 is pinned");
  check_near(repeat_audit.max_delta_e_76, 1.952281070158, 1e-9,
             "reference repeat: D55/2-degree maximum Delta E 76 is pinned");
}
