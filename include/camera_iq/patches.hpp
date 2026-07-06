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

namespace camera_iq {

struct RawMeta;

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

struct RawDiggerPatchTable {
  std::vector<PatchCoord> coords;
  std::vector<CameraRgbPatch> reference_rgb;
  std::vector<std::string> sample_names;
};

struct FlatFieldCorrectionSummary {
  // Per-channel mean of flat samples above floor_value. This intentionally uses
  // the valid-sample mean, not the image maximum, so one hot/near-clipped sample
  // cannot define the correction scale.
  CameraRgbPatch normalizer;
  double floor_value = 0;
  std::size_t pixel_count = 0;
  std::size_t valid_sample_count = 0;
  std::size_t clamped_sample_count = 0;
  std::size_t near_ceiling_sample_count = 0;
  double near_ceiling_fraction = 0;
  double max_allowed_near_ceiling_fraction = 0;
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

void write_camera_rgb_csv(std::ostream& os,
                          const std::vector<PatchMean>& patches);

std::string_view flat_field_normalization_policy();
double flat_field_near_ceiling_threshold_fraction();

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
    const std::optional<PatchGeometryReport>& geometry = std::nullopt);

}  // namespace camera_iq
