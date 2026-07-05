#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/demosaic.hpp"

namespace camera_iq {

struct PatchCoord {
  double x = 0;
  double y = 0;
  double width = 0;
  double height = 0;
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

}  // namespace camera_iq
