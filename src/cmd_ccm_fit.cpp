#include "camera_iq/commands.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/colorimetry.hpp"
#include "camera_iq/dataset_config.hpp"
#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

struct Args {
  std::string dataset_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path illuminant_spd;
  std::filesystem::path camera_rgb;
  std::filesystem::path out;
};

void usage() {
  std::cerr << "Usage: camera_iq ccm-fit <dataset-id> --illuminant-spd FILE"
               " [--config FILE] [--camera-rgb FILE] [--out FILE]\n"
               "Fits a linear RGB-to-XYZ matrix against the dataset's "
               "configured spectral reference.\n";
}

SpectralReferenceProvenance provenance_from_spec(
    const ColorReferenceSpec& spec) {
  SpectralReferenceProvenance provenance;
  provenance.source = spec.source;
  provenance.illuminant = spec.illuminant;
  provenance.observer = spec.observer;
  provenance.unit = spec.unit;
  provenance.numbering_order = spec.numbering_order;
  return provenance;
}

SpectralReferenceValidation validation_from_spec(
    const ColorReferenceSpec& spec) {
  SpectralReferenceValidation validation;
  validation.expected_patch_count = spec.expected_patch_count;
  validation.expected_band_count = spec.expected_band_count;
  validation.first_wavelength_nm = spec.first_wavelength_nm;
  validation.last_wavelength_nm = spec.last_wavelength_nm;
  validation.min_reflectance = spec.min_reflectance;
  validation.max_reflectance = spec.max_reflectance;
  return validation;
}

SpectralReferencePairingThresholds pairing_thresholds_from_spec(
    const ColorReferenceSpec& spec) {
  SpectralReferencePairingThresholds thresholds;
  if (spec.pairing_min_luminance_correlation) {
    thresholds.min_luminance_correlation =
        *spec.pairing_min_luminance_correlation;
  }
  if (spec.pairing_min_red_green_correlation) {
    thresholds.min_red_green_correlation =
        *spec.pairing_min_red_green_correlation;
  }
  if (spec.pairing_min_blue_green_correlation) {
    thresholds.min_blue_green_correlation =
        *spec.pairing_min_blue_green_correlation;
  }
  return thresholds;
}

SpectralReference read_reference_from_spec(const ColorReferenceSpec& spec) {
  const auto provenance = provenance_from_spec(spec);
  if (spec.format == "camera_iq_spectral_csv") {
    return read_spectral_reference_csv(spec.path, spec.id, provenance);
  }
  if (spec.format == "cgats_spectral") {
    return read_spectral_reference_cgats(spec.path, spec.id, provenance);
  }
  throw std::runtime_error("unsupported reference format '" + spec.format +
                           "' for " + spec.id);
}

void write_xyz(JsonWriter& w, const Xyz& xyz) {
  w.begin_object();
  w.key("x");
  w.value(xyz.x);
  w.key("y");
  w.value(xyz.y);
  w.key("z");
  w.value(xyz.z);
  w.end_object();
}

void write_matrix(JsonWriter& w,
                  const std::array<std::array<double, 3>, 3>& matrix) {
  w.begin_array();
  for (const auto& row : matrix) {
    w.begin_array();
    for (const double value : row) {
      w.value(value);
    }
    w.end_array();
  }
  w.end_array();
}

void write_pairing(JsonWriter& w, const SpectralReferencePairing& pairing) {
  w.begin_object();
  w.key("method");
  w.value("broadband_luminance_and_chroma_proxy_correlation");
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(pairing.patch_count));
  w.key("luminance_correlation");
  w.value(pairing.luminance_correlation);
  w.key("red_green_correlation");
  w.value(pairing.red_green_correlation);
  w.key("blue_green_correlation");
  w.value(pairing.blue_green_correlation);
  w.key("min_luminance_correlation");
  w.value(pairing.thresholds.min_luminance_correlation);
  w.key("min_red_green_correlation");
  w.value(pairing.thresholds.min_red_green_correlation);
  w.key("min_blue_green_correlation");
  w.value(pairing.thresholds.min_blue_green_correlation);
  w.key("passes");
  w.value(pairing.passes);
  w.end_object();
}

void write_report(std::ostream& os, const std::string& dataset_id,
                  const ColorReferenceSpec& spec,
                  const std::filesystem::path& camera_rgb_path,
                  const std::filesystem::path& illuminant_path,
                  const RenderedReference& rendered, const CcmFit& fit,
                  const SpectralReferencePairing& pairing) {
  JsonWriter w(os);
  w.begin_object();
  w.key("dataset_id");
  w.value(dataset_id);
  w.key("reference_id");
  w.value(spec.id);
  w.key("reference_role");
  w.value(spec.role);
  w.key("reference_scope");
  w.value("compatible_sg_spectral_not_exact_per_unit");
  w.key("selection_basis");
  w.value(spec.selection_basis);
  w.key("reference_path");
  w.value(spec.path.generic_string());
  w.key("camera_rgb_path");
  w.value(camera_rgb_path.generic_string());
  w.key("illuminant_spd_path");
  w.value(illuminant_path.generic_string());
  w.key("observer");
  w.value("CIE 1931 2 degree");
  w.key("reference_unit");
  w.value(spec.unit);
  w.key("white_xyz");
  write_xyz(w, rendered.white_xyz);
  w.key("matrix_rgb_to_xyz");
  write_matrix(w, fit.matrix);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(fit.patch_count));
  w.key("delta_e_76");
  w.begin_object();
  w.key("mean");
  w.value(fit.mean_delta_e_76);
  w.key("rms");
  w.value(fit.rms_delta_e_76);
  w.key("max");
  w.value(fit.max_delta_e_76);
  w.end_object();
  w.key("pairing");
  write_pairing(w, pairing);
  w.key("limitations");
  w.begin_array();
  w.value("Compatible SG spectral reference, not proven exact per-unit chart");
  w.value("Linear 3x3 CCM only; root-polynomial fit is not implemented here");
  w.value("DeltaE76 summary, not CIEDE2000 or ISO color accuracy claim");
  w.value(
      "Uses the supplied illuminant SPD; illumination stability is not verified");
  w.value(
      "Camera RGB input is a patch table; upstream extraction/correction "
      "provenance must be tracked by the producer");
  w.end_array();
  w.end_object();
}

}  // namespace

int cmd_ccm_fit(int argc, char** argv) {
  Args args;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config") {
      if (++i >= argc) {
        std::cerr << "camera_iq ccm-fit: --config requires a path\n";
        return 2;
      }
      args.config = argv[i];
    } else if (arg == "--illuminant-spd") {
      if (++i >= argc) {
        std::cerr << "camera_iq ccm-fit: --illuminant-spd requires a path\n";
        return 2;
      }
      args.illuminant_spd = argv[i];
    } else if (arg == "--camera-rgb") {
      if (++i >= argc) {
        std::cerr << "camera_iq ccm-fit: --camera-rgb requires a path\n";
        return 2;
      }
      args.camera_rgb = argv[i];
    } else if (arg == "--out") {
      if (++i >= argc) {
        std::cerr << "camera_iq ccm-fit: --out requires a path\n";
        return 2;
      }
      args.out = argv[i];
    } else if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (args.dataset_id.empty()) {
      args.dataset_id = arg;
    } else {
      std::cerr << "camera_iq ccm-fit: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }

  if (args.dataset_id.empty() || args.illuminant_spd.empty()) {
    usage();
    return 2;
  }

  try {
    const auto datasets = read_dataset_config(args.config);
    const auto it = datasets.find(args.dataset_id);
    if (it == datasets.end()) {
      std::cerr << "camera_iq ccm-fit: dataset not found: "
                << args.dataset_id << "\n";
      return 1;
    }
    if (!it->second.color_reference) {
      std::cerr << "camera_iq ccm-fit: dataset has no color_reference: "
                << args.dataset_id << "\n";
      return 1;
    }

    const auto spec = *it->second.color_reference;
    auto ref = read_reference_from_spec(spec);
    validate_spectral_reference(ref, validation_from_spec(spec));

    const auto camera_rgb_path =
        args.camera_rgb.empty() ? spec.pairing_rgb_path : args.camera_rgb;
    if (camera_rgb_path.empty()) {
      std::cerr << "camera_iq ccm-fit: no camera RGB path configured; pass "
                   "--camera-rgb FILE\n";
      return 2;
    }
    const auto camera_rgb = read_camera_rgb_csv(camera_rgb_path);
    const auto pairing = evaluate_reference_pairing(
        ref, camera_rgb, pairing_thresholds_from_spec(spec));
    if (!pairing.passes) {
      std::cerr << "camera_iq ccm-fit: reference/camera pairing failed "
                   "configured correlation thresholds\n";
      return 1;
    }

    const auto illuminant =
        read_spectrum_csv_interpolated(args.illuminant_spd,
                                       ref.wavelengths_nm);
    const auto rendered = render_reference_xyz(ref, illuminant);
    const auto fit =
        fit_rgb_to_xyz_ccm(camera_rgb, rendered.patch_xyz, rendered.white_xyz);

    if (args.out.empty()) {
      write_report(std::cout, args.dataset_id, spec, camera_rgb_path,
                   args.illuminant_spd, rendered, fit, pairing);
      std::cout << "\n";
    } else {
      std::ofstream os(args.out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq ccm-fit: cannot write " << args.out << "\n";
        return 1;
      }
      write_report(os, args.dataset_id, spec, camera_rgb_path,
                   args.illuminant_spd, rendered, fit, pairing);
      os << "\n";
      std::cerr << "CCM fit report written to " << args.out << "\n";
    }
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq ccm-fit: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace camera_iq
