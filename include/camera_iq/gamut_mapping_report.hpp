#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/gamut_mapping.hpp"

namespace camera_iq {

inline constexpr int kGamutMapSchemaVersion = 3;

struct GamutSampleInput {
  std::string id;
  EncodedRgb encoded;
};

struct GamutSampleReport {
  std::string id;
  EncodedRgb input_encoded;
  GamutMappingResult mapping;
  std::string branch;
  double input_hue_degrees = 0;
  double output_hue_degrees = 0;
  double delta_e_2000 = 0;
  double delta_e_ok = 0;
  double delta_lightness = 0;
  double delta_chroma = 0;
  double delta_hue_degrees = 0;
  double input_ipt_hue_degrees = 0;
  double output_ipt_hue_degrees = 0;
  double delta_ipt_hue_degrees = 0;
  bool ipt_hue_defined = false;
  double destination_margin_before = 0;
  double destination_margin_after = 0;
  double destination_boundary_utilization = 0;
};

struct GamutMapReport {
  GamutMapOptions options;
  std::string input_label;
  std::string input_sha256;
  std::vector<GamutSampleReport> samples;
  std::size_t out_of_gamut_count = 0;
  std::size_t modified_count = 0;
  double mean_delta_e_2000 = 0;
  double max_delta_e_2000 = 0;
  double mean_delta_e_ok = 0;
  double max_delta_e_ok = 0;
  double max_abs_delta_lightness = 0;
  double max_abs_delta_hue_degrees = 0;
  std::size_t ipt_hue_sample_count = 0;
  std::size_t ipt_hue_above_3_degrees_count = 0;
  double median_abs_delta_ipt_hue_degrees = 0;
  double p90_abs_delta_ipt_hue_degrees = 0;
  double max_abs_delta_ipt_hue_degrees = 0;
};

std::vector<GamutSampleInput> read_gamut_samples_csv(
    const std::filesystem::path& path);

std::vector<GamutSampleInput> parse_gamut_samples_csv(std::string_view bytes);

GamutMapReport analyze_gamut_samples(
    const std::vector<GamutSampleInput>& samples,
    const GamutMapOptions& options, std::string_view input_label,
    std::string_view input_sha256);

void write_gamut_map_json(std::ostream& os, const GamutMapReport& report);
void write_gamut_map_csv(std::ostream& os, const GamutMapReport& report);

std::string_view rgb_color_space_name(RgbColorSpace space);
std::string_view gamut_map_algorithm_name(GamutMapIntent intent);
std::string_view gamut_mapping_coordinate_space_name(
    GamutMappingCoordinateSpace space);

}  // namespace camera_iq
