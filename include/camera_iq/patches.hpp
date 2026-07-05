#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
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

PatchComparison compare_patch_means_to_rgb(
    const std::vector<PatchMean>& patches,
    const std::vector<CameraRgbPatch>& reference_rgb);

}  // namespace camera_iq
