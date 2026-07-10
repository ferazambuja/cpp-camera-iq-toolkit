#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/dataset_config.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"

namespace camera_iq {
namespace {

struct Args {
  std::string target;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path camera_rgb;
  std::filesystem::path out;
};

void usage() {
  std::cerr << "Usage: camera_iq reference-info <dataset-id|spectral-csv>"
               " [--config FILE] [--camera-rgb FILE] [--out FILE]\n"
               "Supported formats: camera_iq_spectral_csv, cgats_spectral\n";
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

void write_report(std::ostream& os, const std::string& dataset_id,
                  const ColorReferenceSpec& spec,
                  const SpectralReferenceSummary& summary,
                  const std::optional<SpectralReferencePairing>& pairing) {
  JsonWriter w(os);
  w.begin_object();
  w.key("dataset_id");
  if (dataset_id.empty()) w.null(); else w.value(dataset_id);
  w.key("reference_id");
  w.value(spec.id);
  w.key("role");
  w.value(spec.role);
  w.key("format");
  w.value(spec.format);
  w.key("selection_basis");
  w.value(spec.selection_basis);
  w.key("provenance");
  w.begin_object();
  w.key("source");
  w.value(summary.provenance.source);
  w.key("illuminant");
  w.value(summary.provenance.illuminant);
  w.key("observer");
  w.value(summary.provenance.observer);
  w.key("unit");
  w.value(summary.provenance.unit);
  w.key("numbering_order");
  w.value(summary.provenance.numbering_order);
  w.end_object();
  w.key("path");
  w.value(spec.path.generic_string());
  w.key("source_xlsx");
  if (spec.source_xlsx.empty()) {
    w.null();
  } else {
    w.value(spec.source_xlsx.generic_string());
  }
  w.key("source_sheet");
  if (spec.source_sheet.empty()) {
    w.null();
  } else {
    w.value(spec.source_sheet);
  }
  w.key("summary");
  w.begin_object();
  w.key("source_label");
  w.value(summary.source_label);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(summary.patch_count));
  w.key("band_count");
  w.value(static_cast<std::int64_t>(summary.band_count));
  w.key("first_wavelength_nm");
  w.value(summary.first_wavelength_nm);
  w.key("last_wavelength_nm");
  w.value(summary.last_wavelength_nm);
  w.key("first_patch_id");
  w.value(summary.first_patch_id);
  w.key("last_patch_id");
  w.value(summary.last_patch_id);
  w.key("min_reflectance");
  w.value(summary.min_reflectance);
  w.key("max_reflectance");
  w.value(summary.max_reflectance);
  w.end_object();
  w.key("validation");
  w.begin_object();
  w.key("passed");
  w.value(true);
  w.end_object();
  w.key("pairing");
  if (!pairing) {
    w.null();
  } else {
    w.begin_object();
    w.key("method");
    w.value("broadband_luminance_and_chroma_proxy_correlation");
    w.key("patch_count");
    w.value(static_cast<std::int64_t>(pairing->patch_count));
    w.key("luminance_correlation");
    w.value(pairing->luminance_correlation);
    w.key("red_green_correlation");
    w.value(pairing->red_green_correlation);
    w.key("blue_green_correlation");
    w.value(pairing->blue_green_correlation);
    w.key("min_luminance_correlation");
    w.value(pairing->thresholds.min_luminance_correlation);
    w.key("min_red_green_correlation");
    w.value(pairing->thresholds.min_red_green_correlation);
    w.key("min_blue_green_correlation");
    w.value(pairing->thresholds.min_blue_green_correlation);
    w.key("passes");
    w.value(pairing->passes);
    w.end_object();
  }
  w.end_object();
}

}  // namespace

int cmd_reference_info(int argc, char** argv) {
  Args args;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config") {
      if (++i >= argc) {
        std::cerr << "camera_iq reference-info: --config requires a path\n";
        return 2;
      }
      args.config = argv[i];
    } else if (arg == "--camera-rgb") {
      if (++i >= argc) {
        std::cerr << "camera_iq reference-info: --camera-rgb requires a path\n";
        return 2;
      }
      args.camera_rgb = argv[i];
    } else if (arg == "--out") {
      if (++i >= argc) {
        std::cerr << "camera_iq reference-info: --out requires a path\n";
        return 2;
      }
      args.out = argv[i];
    } else if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (args.target.empty()) {
      args.target = arg;
    } else {
      std::cerr << "camera_iq reference-info: unexpected argument '" << arg
                << "'\n";
      return 2;
    }
  }

  if (args.target.empty()) {
    usage();
    return 2;
  }

  try {
    std::string dataset_id;
    ColorReferenceSpec spec;
    const std::filesystem::path direct{args.target};
    if (std::filesystem::is_regular_file(direct)) {
      spec.id = "direct";
      spec.role = "direct_spectral_reference";
      spec.format = "camera_iq_spectral_csv";
      spec.path = direct;
      spec.selection_basis = "explicit_file";
      if (spec.unit.empty()) spec.unit = "spectral_reflectance";
    } else {
      const auto datasets = read_dataset_config(args.config);
      const auto it = datasets.find(args.target);
      if (it == datasets.end()) {
        std::cerr << "camera_iq reference-info: dataset not found: "
                  << args.target << "\n";
        return 1;
      }
      dataset_id = it->first;
      if (!it->second.color_reference) {
        std::cerr << "camera_iq reference-info: dataset has no color_reference: "
                  << args.target << "\n";
        return 1;
      }
      spec = *it->second.color_reference;
    }

    SpectralReference ref;
    const auto provenance = provenance_from_spec(spec);
    if (spec.format == "camera_iq_spectral_csv") {
      ref = read_spectral_reference_csv(spec.path, spec.id, provenance);
    } else if (spec.format == "cgats_spectral") {
      ref = read_spectral_reference_cgats(spec.path, spec.id, provenance);
    } else {
      std::cerr << "camera_iq reference-info: unsupported reference format '"
                << spec.format << "' for " << spec.id << "\n";
      return 1;
    }

    validate_spectral_reference(ref, validation_from_spec(spec));
    std::optional<SpectralReferencePairing> pairing;
    const auto pairing_path = args.camera_rgb.empty() ? spec.pairing_rgb_path
                                                      : args.camera_rgb;
    if (!pairing_path.empty()) {
      pairing = evaluate_reference_pairing(
          ref, read_camera_rgb_csv(pairing_path),
          pairing_thresholds_from_spec(spec));
      if (!pairing->passes) {
        std::cerr << "camera_iq reference-info: reference/camera pairing "
                     "failed configured correlation thresholds\n";
        return 1;
      }
    }

    const auto summary = summarize_spectral_reference(ref);
    if (args.out.empty()) {
      write_report(std::cout, dataset_id, spec, summary, pairing);
      std::cout << "\n";
    } else {
      if (!write_output_file_checked(
              args.out, "reference-info",
              [&](std::ostream& os) {
                write_report(os, dataset_id, spec, summary, pairing);
              },
              std::cerr)) {
        return 1;
      }
      std::cerr << "reference info written to " << args.out << "\n";
    }
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq reference-info: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace camera_iq
