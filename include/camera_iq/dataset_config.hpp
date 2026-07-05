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
};

struct DatasetSpec {
  std::string id;
  std::filesystem::path root;
  std::string description;
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

}  // namespace camera_iq
