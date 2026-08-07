#pragma once

#include <filesystem>
#include <iosfwd>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace camera_iq {

struct SpectralReferencePatch {
  std::string id;
  // CGATS sequence identity and layout label are distinct. `id` remains the
  // explicitly selected operational identifier used by existing consumers.
  std::string sample_id;
  std::string sample_name;
  std::optional<std::array<double, 3>> declared_lab;
  std::optional<std::array<double, 3>> declared_xyz;
  std::vector<double> reflectance;
};

struct CgatsSchemaDiagnostics {
  std::optional<std::size_t> declared_field_count;
  std::size_t actual_field_count = 0;
  bool field_count_matches = true;
  std::optional<std::size_t> declared_set_count;
  std::size_t actual_set_count = 0;
  bool set_count_matches = true;
  std::vector<std::string> observer_declarations;
  bool observer_declarations_conflict = false;
};

struct SpectralReferenceProvenance {
  std::string source;
  std::string illuminant;
  std::string observer;
  std::string unit;
  std::string numbering_order;
};

struct SpectralReference {
  std::string source_label;
  SpectralReferenceProvenance provenance;
  std::vector<double> wavelengths_nm;
  std::vector<SpectralReferencePatch> patches;
  std::optional<CgatsSchemaDiagnostics> cgats_schema;
};

struct SpectralReferenceInterchangeComparison {
  bool stable_id_set_matches = false;
  bool spectra_match_by_stable_id = false;
  bool layout_labels_match_by_stable_id = false;
  bool exact_spectral_multiset_identity = false;
  std::vector<std::size_t> right_index_by_left;
};

struct SpectralReferenceSummary {
  std::string source_label;
  std::size_t patch_count = 0;
  std::size_t band_count = 0;
  double first_wavelength_nm = 0;
  double last_wavelength_nm = 0;
  std::string first_patch_id;
  std::string last_patch_id;
  double min_reflectance = 0;
  double max_reflectance = 0;
  SpectralReferenceProvenance provenance;
};

struct SpectralReferenceValidation {
  std::optional<std::size_t> expected_patch_count;
  std::optional<std::size_t> expected_band_count;
  std::optional<double> first_wavelength_nm;
  std::optional<double> last_wavelength_nm;
  std::optional<double> min_reflectance;
  std::optional<double> max_reflectance;
};

struct CameraRgbPatch {
  double r = 0;
  double g = 0;
  double b = 0;
};

struct SpectralReferencePairingThresholds {
  double min_luminance_correlation = 0.0;
  double min_red_green_correlation = 0.0;
  double min_blue_green_correlation = 0.0;
};

struct SpectralReferencePairing {
  std::size_t patch_count = 0;
  double luminance_correlation = 0;
  double red_green_correlation = 0;
  double blue_green_correlation = 0;
  SpectralReferencePairingThresholds thresholds;
  bool passes = false;
};

struct SpectralReferenceOrientationScore {
  std::string orientation;
  SpectralReferencePairing pairing;
  double aggregate_score = 0;
};

struct SpectralReferenceOrientationReport {
  std::string method =
      "broadband_luminance_and_chroma_proxy_correlation_with_flip_controls";
  std::vector<SpectralReferenceOrientationScore> scores;
  std::string best_orientation;
  bool orientation_valid = false;
};

// Canonical private/export format:
// patch_id,380,390,...,730
// A1,0.1123,0.2108,...
// ...
// The C++ core intentionally ingests this stable text form instead of depending
// on an Excel/ZIP parser. Use tools/export_ccsg_xlsx.py for local xlsx sources.
SpectralReference read_spectral_reference_csv(
    const std::filesystem::path& path, std::string source_label = {},
    SpectralReferenceProvenance provenance = {});

// Reads CGATS files with explicit SPECTRAL_NM### columns, such as the 2016
// PatchTool/i1Pro SG measurements. Both SAMPLE_ID and SAMPLE_NAME are retained.
// The legacy operational `id` uses SAMPLE_NAME when present, otherwise
// SAMPLE_ID; comparisons that claim physical identity must select SAMPLE_ID
// explicitly rather than inheriting that layout-oriented choice.
SpectralReference read_spectral_reference_cgats(
    const std::filesystem::path& path, std::string source_label = {},
    SpectralReferenceProvenance provenance = {});

SpectralReferenceSummary summarize_spectral_reference(
    const SpectralReference& ref);

SpectralReferenceInterchangeComparison
compare_spectral_reference_interchange(const SpectralReference& left,
                                       const SpectralReference& right);

void validate_spectral_reference(const SpectralReference& ref,
                                 const SpectralReferenceValidation& rule);

std::vector<CameraRgbPatch> read_camera_rgb_csv(
    const std::filesystem::path& path);

SpectralReferencePairing evaluate_reference_pairing(
    const SpectralReference& ref, const std::vector<CameraRgbPatch>& camera_rgb,
    SpectralReferencePairingThresholds thresholds = {});

SpectralReferenceOrientationReport evaluate_reference_orientation_controls(
    const SpectralReference& ref, const std::vector<CameraRgbPatch>& camera_rgb,
    SpectralReferencePairingThresholds thresholds = {});

void write_spectral_reference_summary_json(std::ostream& os,
                                           const SpectralReferenceSummary& s);

}  // namespace camera_iq
