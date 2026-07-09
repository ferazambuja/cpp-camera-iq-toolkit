#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace camera_iq {

struct ColorReferenceSpec {
  std::string id;
  std::string role;
  std::string format;
  std::filesystem::path path;
  std::filesystem::path source_xlsx;
  std::string source_sheet;
  std::string selection_basis;
  std::string source;
  std::string reference_project;
  std::string reference_year;
  std::string physical_chart_identity;
  std::string illuminant;
  std::string observer;
  std::string unit;
  std::string numbering_order;
  std::optional<std::size_t> expected_patch_count;
  std::optional<std::size_t> expected_band_count;
  std::optional<double> first_wavelength_nm;
  std::optional<double> last_wavelength_nm;
  std::optional<double> min_reflectance;
  std::optional<double> max_reflectance;
  std::filesystem::path pairing_rgb_path;
  std::optional<double> pairing_min_luminance_correlation;
  std::optional<double> pairing_min_red_green_correlation;
  std::optional<double> pairing_min_blue_green_correlation;
};

struct DatasetSpec {
  std::string id;
  std::filesystem::path root;
  std::string description;
  std::string capture_project;
  std::string capture_year;
  std::string timeline_note;
  std::optional<ColorReferenceSpec> color_reference;
};

struct ResolvedDataset {
  std::string id;
  std::filesystem::path root;
  std::string label;
  bool from_config = false;
};

std::filesystem::path default_dataset_config_path();

std::map<std::string, DatasetSpec> read_dataset_config(
    const std::filesystem::path& config_path);

std::optional<ResolvedDataset> resolve_dataset_root(
    std::string_view root_or_id,
    const std::filesystem::path& config_path = default_dataset_config_path());

std::string dataset_root_label(std::string_view dataset_id);
std::string dataset_file_label(std::string_view dataset_id,
                               const std::filesystem::path& relative_path);

// Display label for evidence JSON that never echoes an absolute path:
// config-resolved datasets keep the redacted "dataset:<id>" label; direct
// paths reduce to "dataset-root:<basename>".
std::string dataset_display_label(const ResolvedDataset& dataset);

}  // namespace camera_iq
