#include "camera_iq/commands.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/colorimetry.hpp"
#include "camera_iq/dataset_config.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"

namespace camera_iq {
namespace {

constexpr std::size_t kDefaultCrossValidationFolds = 5;

struct Args {
  std::string dataset_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path illuminant_spd;
  std::filesystem::path camera_rgb;
  std::filesystem::path out;
  std::optional<double> exclude_ref_lightness_below;
};

struct PatchSubset {
  std::vector<CameraRgbPatch> camera_rgb;
  std::vector<Xyz> target_xyz;
};

void usage() {
  std::cerr << "Usage: camera_iq ccm-fit <dataset-id> --illuminant-spd FILE"
               " [--config FILE] [--camera-rgb FILE]"
               " [--exclude-ref-lightness-below LSTAR] [--out FILE]\n"
               "Fits a linear RGB-to-XYZ matrix against the dataset's "
               "configured spectral reference.\n";
}

PatchSubset subset_patches(const std::vector<CameraRgbPatch>& camera_rgb,
                           const std::vector<Xyz>& target_xyz,
                           const std::vector<std::size_t>& indices) {
  PatchSubset out;
  out.camera_rgb.reserve(indices.size());
  out.target_xyz.reserve(indices.size());
  for (const std::size_t index : indices) {
    if (index >= camera_rgb.size() || index >= target_xyz.size()) {
      throw std::runtime_error("ccm fit: patch subset index out of range");
    }
    out.camera_rgb.push_back(camera_rgb[index]);
    out.target_xyz.push_back(target_xyz[index]);
  }
  return out;
}

void require_non_empty(std::string_view value, std::string_view field) {
  if (value.empty()) {
    throw std::runtime_error("ccm fit: missing required provenance field " +
                             std::string(field));
  }
}

void validate_ccm_provenance(const DatasetSpec& dataset,
                             const ColorReferenceSpec& spec) {
  require_non_empty(dataset.capture_project, "capture_project");
  require_non_empty(dataset.capture_year, "capture_year");
  require_non_empty(dataset.timeline_note, "timeline_note");
  require_non_empty(spec.selection_basis, "color_reference.selection_basis");
  require_non_empty(spec.source, "color_reference.source");
  require_non_empty(spec.reference_project, "color_reference.reference_project");
  require_non_empty(spec.reference_year, "color_reference.reference_year");
  require_non_empty(spec.physical_chart_identity,
                    "color_reference.physical_chart_identity");
  require_non_empty(spec.numbering_order, "color_reference.numbering_order");
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

void write_delta_summary(JsonWriter& w, double mean, double rms, double max) {
  w.begin_object();
  w.key("mean");
  w.value(mean);
  w.key("rms");
  w.value(rms);
  w.key("max");
  w.value(max);
  w.end_object();
}

void write_evaluation(JsonWriter& w, const CcmEvaluation& evaluation) {
  w.begin_object();
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(evaluation.patch_count));
  w.key("delta_e_76");
  write_delta_summary(w, evaluation.mean_delta_e_76, evaluation.rms_delta_e_76,
                      evaluation.max_delta_e_76);
  w.key("delta_e_2000");
  write_delta_summary(w, evaluation.mean_delta_e_2000,
                      evaluation.rms_delta_e_2000,
                      evaluation.max_delta_e_2000);
  w.end_object();
}

void write_fit_as_evaluation(JsonWriter& w, const CcmFit& fit) {
  CcmEvaluation evaluation;
  evaluation.patch_count = fit.patch_count;
  evaluation.mean_delta_e_76 = fit.mean_delta_e_76;
  evaluation.rms_delta_e_76 = fit.rms_delta_e_76;
  evaluation.max_delta_e_76 = fit.max_delta_e_76;
  evaluation.mean_delta_e_2000 = fit.mean_delta_e_2000;
  evaluation.rms_delta_e_2000 = fit.rms_delta_e_2000;
  evaluation.max_delta_e_2000 = fit.max_delta_e_2000;
  write_evaluation(w, evaluation);
}

void write_patch_ids(JsonWriter& w, const SpectralReference& ref,
                     const std::vector<std::size_t>& indices) {
  w.begin_array();
  for (const std::size_t index : indices) {
    if (index < ref.patches.size()) {
      w.value(ref.patches[index].id);
    }
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

void write_timeline_provenance(JsonWriter& w, const DatasetSpec& dataset,
                               const ColorReferenceSpec& spec) {
  w.begin_object();
  w.key("selection_basis");
  w.value(spec.selection_basis);
  w.key("capture_project");
  w.value(dataset.capture_project);
  w.key("capture_year");
  w.value(dataset.capture_year);
  w.key("reference_project");
  w.value(spec.reference_project);
  w.key("reference_year");
  w.value(spec.reference_year);
  w.key("reference_source");
  w.value(spec.source);
  w.key("physical_chart_identity");
  w.value(spec.physical_chart_identity);
  w.key("timeline_note");
  w.value(dataset.timeline_note);
  w.end_object();
}

void write_lightness_selection(JsonWriter& w,
                               const std::optional<double>& threshold,
                               const CcmLightnessSelection& selection,
                               const SpectralReference& ref) {
  w.begin_object();
  w.key("enabled");
  w.value(threshold.has_value());
  w.key("method");
  w.value("exclude_rendered_reference_lab_lstar_below_threshold");
  w.key("threshold_lstar");
  if (threshold) {
    w.value(*threshold);
  } else {
    w.null();
  }
  w.key("kept_patch_count");
  w.value(static_cast<std::int64_t>(selection.kept_indices.size()));
  w.key("excluded_patch_count");
  w.value(static_cast<std::int64_t>(selection.excluded_indices.size()));
  w.key("excluded_reference_patch_ids");
  write_patch_ids(w, ref, selection.excluded_indices);
  w.key("rationale");
  if (threshold) {
    w.value("Dark SG patches can be flare-lifted in camera captures relative "
            "to contact/spectro references; exclusion is explicit and "
            "reference-lightness based.");
  } else {
    w.value("No lightness exclusion requested; all configured reference patches "
            "are used for the fit.");
  }
  w.end_object();
}

void write_dark_diagnostics(JsonWriter& w,
                            const CcmDarkPatchDiagnostics& diagnostics,
                            const SpectralReference& ref) {
  w.begin_object();
  w.key("method");
  w.value("target_reference_lab_lstar_below_threshold");
  w.key("max_lstar");
  w.value(diagnostics.max_lstar);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(diagnostics.patch_count));
  w.key("worst_patch_metric");
  w.value("delta_e_76");
  w.key("worst_patch_index_zero_based");
  if (diagnostics.worst_patch_index < ref.patches.size()) {
    w.value(static_cast<std::int64_t>(diagnostics.worst_patch_index));
  } else {
    w.null();
  }
  w.key("worst_patch_id");
  if (diagnostics.worst_patch_index < ref.patches.size()) {
    w.value(ref.patches[diagnostics.worst_patch_index].id);
  } else {
    w.null();
  }
  w.key("delta_e_76");
  write_delta_summary(w, diagnostics.mean_delta_e_76,
                      diagnostics.rms_delta_e_76,
                      diagnostics.max_delta_e_76);
  w.key("delta_e_2000");
  write_delta_summary(w, diagnostics.mean_delta_e_2000,
                      diagnostics.rms_delta_e_2000,
                      diagnostics.max_delta_e_2000);
  w.end_object();
}

void write_report(std::ostream& os, const std::string& dataset_id,
                  const DatasetSpec& dataset,
                  const ColorReferenceSpec& spec,
                  const std::filesystem::path& camera_rgb_path,
                  const std::filesystem::path& illuminant_path,
                  const SpectralReference& ref,
                  const RenderedReference& rendered, const CcmFit& fit,
                  const CcmCrossValidation& cv,
                  const CcmFit& baseline_all_patch_fit,
                  const CcmCrossValidation& baseline_cv,
                  const CcmEvaluation& all_patches_with_fit_matrix,
                  const CcmEvaluation& fit_set_evaluation,
                  const std::optional<CcmEvaluation>& excluded_evaluation,
                  const std::optional<double>& exclusion_threshold,
                  const CcmLightnessSelection& lightness_selection,
                  const CcmDarkPatchDiagnostics& dark_diagnostics,
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
  w.key("timeline_provenance");
  write_timeline_provenance(w, dataset, spec);
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
  w.key("reference_numbering_order");
  w.value(spec.numbering_order);
  w.key("model");
  w.value("linear_3x3");
  w.key("white_xyz");
  write_xyz(w, rendered.white_xyz);
  w.key("matrix_rgb_to_xyz");
  write_matrix(w, fit.matrix);
  w.key("total_patch_count");
  w.value(static_cast<std::int64_t>(rendered.patch_xyz.size()));
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(fit.patch_count));
  w.key("fit_patch_count");
  w.value(static_cast<std::int64_t>(fit.patch_count));
  w.key("delta_e_76");
  write_delta_summary(w, fit.mean_delta_e_76, fit.rms_delta_e_76,
                      fit.max_delta_e_76);
  w.key("delta_e_2000");
  write_delta_summary(w, fit.mean_delta_e_2000, fit.rms_delta_e_2000,
                      fit.max_delta_e_2000);
  w.key("lightness_exclusion");
  write_lightness_selection(w, exclusion_threshold, lightness_selection, ref);
  w.key("evaluations");
  w.begin_object();
  w.key("baseline_all_patch_fit");
  write_fit_as_evaluation(w, baseline_all_patch_fit);
  w.key("fit_set");
  write_evaluation(w, fit_set_evaluation);
  w.key("all_patches_with_fit_matrix");
  write_evaluation(w, all_patches_with_fit_matrix);
  w.key("excluded_patches_with_fit_matrix");
  if (excluded_evaluation) {
    write_evaluation(w, *excluded_evaluation);
  } else {
    w.null();
  }
  w.end_object();
  w.key("cross_validation");
  w.begin_object();
  w.key("method");
  w.value("deterministic_k_fold_by_row_index");
  w.key("fold_count");
  w.value(static_cast<std::int64_t>(cv.fold_count));
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(cv.patch_count));
  w.key("delta_e_76");
  write_delta_summary(w, cv.mean_delta_e_76, cv.rms_delta_e_76,
                      cv.max_delta_e_76);
  w.key("delta_e_2000");
  write_delta_summary(w, cv.mean_delta_e_2000, cv.rms_delta_e_2000,
                      cv.max_delta_e_2000);
  w.end_object();
  w.key("baseline_cross_validation");
  w.begin_object();
  w.key("method");
  w.value("deterministic_k_fold_by_row_index_all_patches_no_exclusion");
  w.key("fold_count");
  w.value(static_cast<std::int64_t>(baseline_cv.fold_count));
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(baseline_cv.patch_count));
  w.key("delta_e_76");
  write_delta_summary(w, baseline_cv.mean_delta_e_76,
                      baseline_cv.rms_delta_e_76, baseline_cv.max_delta_e_76);
  w.key("delta_e_2000");
  write_delta_summary(w, baseline_cv.mean_delta_e_2000,
                      baseline_cv.rms_delta_e_2000,
                      baseline_cv.max_delta_e_2000);
  w.end_object();
  w.key("dark_patch_diagnostics");
  write_dark_diagnostics(w, dark_diagnostics, ref);
  w.key("pairing");
  write_pairing(w, pairing);
  w.key("limitations");
  w.begin_array();
  w.value("Compatible SG spectral reference, not proven exact per-unit chart");
  w.value("Linear 3x3 CCM only; root-polynomial fit is intentionally deferred");
  w.value("Reports DeltaE76 and CIEDE2000; neither is an ISO color accuracy claim");
  w.value("Cross-validation is deterministic row-index k-fold, not a substitute "
          "for an independent physical chart capture");
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
    } else if (arg == "--exclude-ref-lightness-below") {
      if (++i >= argc) {
        std::cerr << "camera_iq ccm-fit: --exclude-ref-lightness-below "
                     "requires an L* threshold\n";
        return 2;
      }
      try {
        std::size_t consumed = 0;
        const std::string text = argv[i];
        const double threshold = std::stod(text, &consumed);
        if (consumed != text.size() || !std::isfinite(threshold) ||
            threshold < 0.0 || threshold > 100.0) {
          throw std::runtime_error("");
        }
        args.exclude_ref_lightness_below = threshold;
      } catch (...) {
        std::cerr << "camera_iq ccm-fit: --exclude-ref-lightness-below "
                     "must be a finite L* value in [0,100]\n";
        return 2;
      }
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
    validate_ccm_provenance(it->second, spec);
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
    CcmLightnessSelection lightness_selection;
    if (args.exclude_ref_lightness_below) {
      lightness_selection = select_reference_lightness(
          rendered.patch_xyz, rendered.white_xyz,
          *args.exclude_ref_lightness_below);
    } else {
      lightness_selection.kept_indices.reserve(rendered.patch_xyz.size());
      for (std::size_t i = 0; i < rendered.patch_xyz.size(); ++i) {
        lightness_selection.kept_indices.push_back(i);
      }
    }
    const auto fit_subset = subset_patches(camera_rgb, rendered.patch_xyz,
                                           lightness_selection.kept_indices);
    const auto excluded_subset = subset_patches(
        camera_rgb, rendered.patch_xyz, lightness_selection.excluded_indices);
    const auto baseline_all_patch_fit =
        fit_rgb_to_xyz_ccm(camera_rgb, rendered.patch_xyz, rendered.white_xyz);
    const auto baseline_cv = cross_validate_rgb_to_xyz_ccm(
        camera_rgb, rendered.patch_xyz, rendered.white_xyz,
        kDefaultCrossValidationFolds);
    const auto fit = fit_rgb_to_xyz_ccm(fit_subset.camera_rgb,
                                        fit_subset.target_xyz,
                                        rendered.white_xyz);
    const auto fit_set_evaluation = evaluate_rgb_to_xyz_ccm(
        fit.matrix, fit_subset.camera_rgb, fit_subset.target_xyz,
        rendered.white_xyz);
    const auto all_patches_with_fit_matrix = evaluate_rgb_to_xyz_ccm(
        fit.matrix, camera_rgb, rendered.patch_xyz, rendered.white_xyz);
    std::optional<CcmEvaluation> excluded_evaluation;
    if (!excluded_subset.camera_rgb.empty()) {
      excluded_evaluation = evaluate_rgb_to_xyz_ccm(
          fit.matrix, excluded_subset.camera_rgb, excluded_subset.target_xyz,
          rendered.white_xyz);
    }
    const auto cv = cross_validate_rgb_to_xyz_ccm(
        fit_subset.camera_rgb, fit_subset.target_xyz, rendered.white_xyz,
        kDefaultCrossValidationFolds);
    const auto dark_diagnostics = diagnose_dark_patches(
        camera_rgb, rendered.patch_xyz, rendered.white_xyz, fit.matrix);

    if (args.out.empty()) {
      write_report(std::cout, args.dataset_id, it->second, spec,
                   camera_rgb_path, args.illuminant_spd, ref, rendered, fit,
                   cv, baseline_all_patch_fit, baseline_cv,
                   all_patches_with_fit_matrix, fit_set_evaluation,
                   excluded_evaluation,
                   args.exclude_ref_lightness_below, lightness_selection,
                   dark_diagnostics, pairing);
      std::cout << "\n";
    } else {
      if (!write_output_file_checked(
              args.out, "ccm-fit",
              [&](std::ostream& os) {
                write_report(os, args.dataset_id, it->second, spec,
                             camera_rgb_path, args.illuminant_spd, ref,
                             rendered, fit, cv, baseline_all_patch_fit,
                             baseline_cv, all_patches_with_fit_matrix,
                             fit_set_evaluation, excluded_evaluation,
                             args.exclude_ref_lightness_below,
                             lightness_selection, dark_diagnostics, pairing);
              },
              std::cerr)) {
        return 1;
      }
      std::cerr << "CCM fit report written to " << args.out << "\n";
    }
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq ccm-fit: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace camera_iq
