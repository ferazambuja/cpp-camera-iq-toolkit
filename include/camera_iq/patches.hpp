#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/demosaic.hpp"
#include "camera_iq/roi.hpp"

namespace camera_iq {

struct RawMeta;
struct RawCfaImage;

struct PatchCoord {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
};

struct PatchGeometryReportPoint {
  double x = 0;
  double y = 0;
};

struct PatchGeometryReportPatch {
  std::string reference_patch_id;
  int row = 0;
  int column = 0;
};

struct PatchGeometryReport {
  std::string chart_model;
  std::string method;
  std::array<PatchGeometryReportPoint, 4> corners;
  std::vector<PatchGeometryReportPatch> patches;
};

struct PatchMean {
  PatchCoord source_coord;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  std::size_t sample_count = 0;
  CameraRgbPatch rgb;
};

struct PatchChannelComparison {
  std::string channel;
  double correlation = 0;
  double slope = 0;
  double intercept = 0;
  double mean_error_before_affine = 0;
  double rmse_before_affine = 0;
  double max_abs_error_before_affine = 0;
  double rmse_after_affine = 0;
};

struct PatchComparison {
  std::size_t patch_count = 0;
  std::array<PatchChannelComparison, 3> channels;
};

struct PatchLocalizationValidationThresholds {
  std::size_t expected_patch_count = 140;
  double max_center_error_px = 5.0;
  double min_channel_correlation = 0.999;
  double max_abs_mean_error_dn = 25.0;
};

struct PatchCenterResidual {
  std::string reference_patch_id;
  int row = 0;
  int column = 0;
  double generated_center_x = 0;
  double generated_center_y = 0;
  double oracle_center_x = 0;
  double oracle_center_y = 0;
  double dx_px = 0;
  double dy_px = 0;
  double distance_px = 0;
};

struct LocalizationMetricSummary {
  std::size_t sample_count = 0;
  double rms_px = 0;
  double max_px = 0;
  double dx_dy_anisotropy = 0;
  double adjacent_vector_cosine = 0;
  double dx_rms_px = 0;
  double dy_rms_px = 0;
};

struct LocalizationHoldoutScore {
  std::string split;
  std::size_t folds = 0;
  LocalizationMetricSummary metrics;
};

struct LocalizationModelReport {
  std::string name;
  std::string hypothesis;
  int degrees_of_freedom = 0;
  bool production_candidate = false;
  LocalizationMetricSummary in_sample;
  std::vector<LocalizationHoldoutScore> heldout_scores;
};

struct LocalizationModelComparison {
  std::string method =
      "diagnostic_model_comparison_spatial_holdouts";
  std::size_t patch_count = 0;
  bool diagnostic_only = true;
  bool predeclared_gate_revision = false;
  bool radial_affine_baselines_reported = false;
  double observed_anisotropy_dx_over_dy = 0;
  double isotropic_radial_predicted_anisotropy_dx_over_dy = 0;
  double noise_floor_px = 0;
  bool noise_floor_usable = false;
  std::string noise_floor_source;
  std::string best_overall_model;
  std::string parsimony_winner_model;
  bool conclusive = false;
  std::string diagnostic_conclusion;
  std::string identifiability_note;
  std::vector<LocalizationModelReport> models;
};

struct IndependentPatchCenter {
  bool valid = false;
  double x = 0;
  double y = 0;
};

struct LocalizationIndependentCenterCheck {
  bool attempted = false;
  std::string method;
  std::size_t valid_count = 0;
  double generated_grid_rms_px = 0;
  double rawdigger_oracle_rms_px = 0;
  std::size_t repeatability_valid_count = 0;
  double repeatability_rms_px = 0;
  std::size_t seed_agreement_valid_count = 0;
  double seed_agreement_rms_px = 0;
  std::string tracks;
  std::string interpretation;
};

struct PatchLocalizationValidation {
  std::string method =
      "rawdigger_oracle_uncorrected_roi_center_and_rgb_mean";
  std::string oracle_label;
  std::string corner_source;
  std::size_t patch_count = 0;
  PatchLocalizationValidationThresholds thresholds;
  double max_center_error_px = 0;
  double rms_center_error_px = 0;
  std::vector<PatchCenterResidual> center_residuals;
  PatchComparison rgb_comparison;
  bool patch_count_gate_passes = false;
  bool center_gate_passes = false;
  bool correlation_gate_passes = false;
  bool mean_error_gate_passes = false;
  bool passes = false;
  std::optional<LocalizationModelComparison> model_comparison;
  std::optional<LocalizationIndependentCenterCheck> independent_center_check;
};

struct RawDiggerPatchTable {
  std::vector<PatchCoord> coords;
  std::vector<CameraRgbPatch> reference_rgb;
  std::vector<std::string> sample_names;
};

struct FlatFieldGateVerdict {
  bool accepted = false;
  std::string reason;
  std::string region;
  int position = -1;
  std::string label;
  double fraction = 0.0;
};

// CFA-domain evidence used to accept or reject a flat before demosaic and
// correction. Fractions remain separate for all four mosaic positions; pooling
// them can hide a failure confined to one color plane.
struct FlatFieldNearCeilingDiagnostics {
  std::string measurement_domain = "raw_cfa_black_subtracted";
  RoiRect frame;
  RoiRect gate;
  double gate_center_fraction = 0.0;
  double near_ceiling_level = 0.0;
  double max_allowed_fraction = 0.0;
  std::array<std::string, 4> labels;
  std::array<double, 4> near_ceiling_fraction_frame{};
  std::array<double, 4> near_ceiling_fraction_gate{};
  FlatFieldGateVerdict verdict;
};

struct FlatFieldCorrectionSummary {
  // Per-channel mean of flat samples above floor_value. This intentionally uses
  // the valid-sample mean, not the image maximum, so one hot/near-ceiling sample
  // cannot define the correction scale.
  CameraRgbPatch normalizer;
  double floor_value = 0;
  std::size_t pixel_count = 0;
  std::size_t valid_sample_count = 0;
  std::size_t clamped_sample_count = 0;
  std::optional<FlatFieldNearCeilingDiagnostics> near_ceiling;
};

struct WhiteBalanceGains {
  double r = 1;
  double g = 1;
  double b = 1;
};

// Reads checker2colors-style coord.csv rows: x,y,width,height. The coordinates
// are MATLAB image coordinates and are interpreted by extract_patch_means() as
// one-based top-left rectangles.
std::vector<PatchCoord> read_patch_coords_csv(
    const std::filesystem::path& path);

// Reads RawDigger's patch CSV export and keeps rows whose Filename matches
// `raw_filename`. RawDigger Left/Top are zero-based pixel coordinates, so they
// are converted to the one-based PatchCoord convention used by extraction.
RawDiggerPatchTable read_rawdigger_patch_table(
    const std::filesystem::path& path, std::string_view raw_filename);

// Extracts mean RGB values from row-major RGB pixels using MATLAB/checker2colors
// one-based rectangle coordinates. Coordinates are rounded to integer pixels,
// shifted to zero-based C++ indices, and clipped to the image bounds.
std::vector<PatchMean> extract_patch_means(
    const std::vector<RgbPixel>& image, int width, int height,
    const std::vector<PatchCoord>& coords);

std::vector<RgbPixel> apply_flat_field(
    const std::vector<RgbPixel>& image, const std::vector<RgbPixel>& flat,
    int width, int height, double floor_value = 1.0,
    FlatFieldCorrectionSummary* summary = nullptr);

std::vector<RgbPixel> apply_white_balance(const std::vector<RgbPixel>& image,
                                          WhiteBalanceGains gains);

WhiteBalanceGains white_balance_gains_from_flat_field(
    const FlatFieldCorrectionSummary& flat);

PatchComparison compare_patch_means_to_rgb(
    const std::vector<PatchMean>& patches,
    const std::vector<CameraRgbPatch>& reference_rgb);

PatchLocalizationValidation validate_patch_localization_against_oracle(
    const std::vector<PatchMean>& patches, const RawDiggerPatchTable& oracle,
    PatchLocalizationValidationThresholds thresholds = {});

void write_camera_rgb_csv(std::ostream& os,
                          const std::vector<PatchMean>& patches);

std::string_view flat_field_normalization_policy();
double flat_field_near_ceiling_threshold_fraction();
double flat_field_center_gate_fraction();

// Measures the same centered, CFA-balanced, per-position near-ceiling gate used
// by `shading`. Returns nullopt when the buffer, geometry, or policy cannot
// define a trustworthy measurement.
std::optional<FlatFieldNearCeilingDiagnostics>
measure_flat_field_near_ceiling(const RawCfaImage& flat, double center_fraction,
                                double near_ceiling_level,
                                double max_allowed_fraction);

// Applies one policy independently to every CFA position in both regions.
// Invalid fractions and policies reject rather than masquerading as passes.
FlatFieldGateVerdict flat_field_near_ceiling_verdict(
    const std::array<double, 4>& frame_fractions,
    const std::array<double, 4>& gate_fractions,
    const std::array<std::string, 4>& labels, double max_allowed);

// Minimal structured result emitted even when a flat is rejected before patch
// extraction. It preserves the evidence that caused the non-zero exit status.
void write_patch_rejection_json(
    std::ostream& os, std::string_view file_label,
    std::string_view flat_label, const RawMeta& meta, int width, int height,
    const FlatFieldNearCeilingDiagnostics& diagnostics);

void write_patch_report_json(
    std::ostream& os, std::string_view file_label,
    std::string_view coords_label, std::string_view coordinate_source_format,
    const RawMeta& meta, int width, int height, std::string_view flat_label,
    const std::optional<FlatFieldCorrectionSummary>& flat,
    const std::optional<WhiteBalanceGains>& wb, std::string_view wb_policy,
    const std::vector<PatchMean>& patches,
    const std::vector<std::string>& sample_names,
    const std::optional<PatchComparison>& comparison,
    std::string_view reference_label,
    const std::optional<PatchGeometryReport>& geometry = std::nullopt,
    const std::optional<SpectralReferenceOrientationReport>& orientation =
        std::nullopt,
    const std::optional<PatchLocalizationValidation>& localization =
        std::nullopt);

}  // namespace camera_iq
