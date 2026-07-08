#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "camera_iq/raw_meta.hpp"
#include "camera_iq/roi.hpp"

namespace camera_iq {

struct SfrOptions {
  double bin_spacing_px = 0.25;
  double min_edge_angle_deg = 2.0;
  double max_edge_angle_deg = 10.0;
  double min_contrast_dn = 20.0;
  double near_saturation_fraction = 0.98;
  int min_roi_dimension_px = 24;
  int min_line_samples = 16;
};

struct SfrResult {
  bool accepted = false;
  std::string rejection_reason;
  std::string channel = "green-linear";
  std::string orientation;
  RoiRect roi;
  int green_sample_count = 0;
  double saturated_fraction = 0.0;
  double contrast_dn = 0.0;
  double edge_angle_deg = 0.0;
  double mtf50_cy_per_px = 0.0;
  double mtf50p_cy_per_px = 0.0;
  double mtf_at_nyquist = 0.0;
  double r1090_px = 0.0;
  double oversample = 4.0;
  std::vector<double> mtf_frequency_cy_per_px;
  std::vector<double> mtf;
};

struct ImatestYMultiOracle {
  std::string filename;
  std::string run_date;
  RoiRect center_roi_full_frame;
  double center_mtf50_cy_per_px = 0.0;
  double center_mtf50p_cy_per_px = 0.0;
};

struct ImatestYMultiRoi {
  int n = 0;
  double distance_percent = 0.0;
  std::string direction_label;
  std::string region_label;
  std::string edge_id;
  std::string csv_summary_file;
  RoiRect full_frame_roi;
  double x_px_from_ctr = 0.0;
  double y_px_from_ctr = 0.0;
  bool edge_id_parsed = false;
  int edge_id_grid_x = 0;
  int edge_id_grid_y = 0;
  std::string edge_id_side;
  std::string edge_id_suffix;
  bool physical_corner = false;
  bool physical_edge = false;
  double imatest_mtf50_cy_per_px = 0.0;
  double imatest_mtf50p_cy_per_px = 0.0;
  double imatest_r1090_px = 0.0;
  double imatest_peak_mtf = 0.0;
};

struct ImatestYMultiFile {
  std::string filename;
  std::string run_date;
  std::vector<ImatestYMultiRoi> rois;
};

struct SfrFieldMtfSummary {
  int roi_count = 0;
  int physical_corner_count = 0;
  int field_argmax_n = 0;
  double field_argmax_mtf50_cy_per_px = 0.0;
  double center_mtf50_cy_per_px = 0.0;
  double physical_corner_max_mtf50_cy_per_px = 0.0;
  bool center_above_physical_corner_max = false;
  bool center_is_field_max = false;
};

struct SfrSweepPoint {
  double aperture = 0.0;
  double mtf50_cy_per_px = 0.0;
};

struct SfrTrendResult {
  bool passed = false;
  double wide_open_max = 0.0;
  double mid_plateau_min = 0.0;
  double f16_value = 0.0;
  double argmax_aperture = 0.0;
  double argmax_mtf50 = 0.0;
};

std::vector<double> dft_magnitude(const std::vector<double>& signal);
double adjacent_difference_response(double frequency_cy_per_px,
                                    double sample_spacing_px);

SfrResult analyze_green_sfr(const RawCfaImage& image, const RoiRect& requested,
                            const SfrOptions& options = {});

std::optional<ImatestYMultiOracle> read_imatest_y_multi(
    const std::filesystem::path& path);

std::optional<ImatestYMultiFile> read_imatest_y_multi_file(
    const std::filesystem::path& path);

std::optional<SfrFieldMtfSummary> summarize_imatest_field_mtf(
    const ImatestYMultiFile& file);

std::optional<RoiRect> full_frame_roi_to_active_area(const RoiRect& full_frame,
                                                     const RawMeta& meta);

SfrTrendResult evaluate_aperture_trend(
    const std::vector<SfrSweepPoint>& points);

}  // namespace camera_iq
