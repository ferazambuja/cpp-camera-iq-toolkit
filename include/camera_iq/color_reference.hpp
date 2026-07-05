#pragma once

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace camera_iq {

struct SpectralReferencePatch {
  std::string id;
  std::vector<double> reflectance;
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
// PatchTool/i1Pro SG measurements. SAMPLE_NAME is used as patch id when present,
// otherwise SAMPLE_ID is used.
SpectralReference read_spectral_reference_cgats(
    const std::filesystem::path& path, std::string source_label = {},
    SpectralReferenceProvenance provenance = {});

SpectralReferenceSummary summarize_spectral_reference(
    const SpectralReference& ref);

void validate_spectral_reference(const SpectralReference& ref,
                                 const SpectralReferenceValidation& rule);

std::vector<CameraRgbPatch> read_camera_rgb_csv(
    const std::filesystem::path& path);

SpectralReferencePairing evaluate_reference_pairing(
    const SpectralReference& ref, const std::vector<CameraRgbPatch>& camera_rgb,
    SpectralReferencePairingThresholds thresholds = {});

void write_spectral_reference_summary_json(std::ostream& os,
                                           const SpectralReferenceSummary& s);

}  // namespace camera_iq
