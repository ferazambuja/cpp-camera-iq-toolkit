#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace camera_iq {

// Reference roles are declarative provenance metadata. Commands must opt into
// the roles whose scientific interpretation they can serialize; parsing a
// role does not imply that every command supports it.
namespace color_reference_roles {
inline constexpr std::string_view kCompatibleSgSpectral =
    "compatible_sg_spectral";
inline constexpr std::string_view kDirectSpectralReference =
    "direct_spectral_reference";
}  // namespace color_reference_roles

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

struct ResolvedFileInput {
  std::filesystem::path actual;
  std::string label;
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

// Publication-safe label for an input that is not attributed to a configured
// dataset. The scope remains explicit while local directory structure is
// reduced to the basename.
std::string public_file_label(const std::filesystem::path& path,
                              std::string_view scope);

// Display label for evidence JSON that never echoes an absolute path:
// config-resolved datasets keep the redacted "dataset:<id>" label; direct
// paths reduce to "dataset-root:<basename>".
std::string dataset_display_label(const ResolvedDataset& dataset);

// Display label for commands that scan an optional dataset-relative subdir.
// Keeps the same privacy contract as dataset_display_label().
std::string dataset_scan_label(const ResolvedDataset& dataset,
                               const std::filesystem::path& relative_subdir);

// A dataset-relative scan path must stay inside the dataset root: relative,
// with no `..` components. `..` would scan outside the root while the JSON
// still labels the evidence under the dataset — provenance misattribution.
bool is_safe_dataset_subdir(const std::filesystem::path& relative_subdir);

// Resolves a dataset-relative input only when its canonical location remains
// within the configured root. Absolute paths, parent traversal, and symlink
// escapes are refused.
std::optional<std::filesystem::path> resolve_dataset_child(
    const std::filesystem::path& root,
    const std::filesystem::path& relative_path);

// Resolves a file argument used alongside a configured dataset. Relative
// paths must remain inside the dataset and retain dataset attribution;
// absolute paths are explicit external evidence and publish only a scoped
// basename.
std::optional<ResolvedFileInput> resolve_dataset_or_external_file(
    const ResolvedDataset& dataset, const std::filesystem::path& path);

// Resolves an optional scan subdirectory. Configured datasets require
// canonical containment; direct-directory mode keeps its non-attributed path
// semantics while retaining the lexical absolute/parent-traversal gate.
std::optional<std::filesystem::path> resolve_dataset_scan_root(
    const ResolvedDataset& dataset,
    const std::filesystem::path& relative_subdir);

}  // namespace camera_iq
