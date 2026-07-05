#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace camera_iq {

struct SpectralReferencePatch {
  std::string id;
  std::vector<double> reflectance;
};

struct SpectralReference {
  std::string source_label;
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
};

// Canonical private/export format:
// patch_id,380,390,...,730
// A1,0.1123,0.2108,...
// ...
// The C++ core intentionally ingests this stable text form instead of depending
// on an Excel/ZIP parser. Use tools/export_ccsg_xlsx.py for local xlsx sources.
SpectralReference read_spectral_reference_csv(
    const std::filesystem::path& path, std::string source_label = {});

// Reads CGATS files with explicit SPECTRAL_NM### columns, such as the 2016
// PatchTool/i1Pro SG measurements. SAMPLE_NAME is used as patch id when present,
// otherwise SAMPLE_ID is used.
SpectralReference read_spectral_reference_cgats(
    const std::filesystem::path& path, std::string source_label = {});

SpectralReferenceSummary summarize_spectral_reference(
    const SpectralReference& ref);

void write_spectral_reference_summary_json(std::ostream& os,
                                           const SpectralReferenceSummary& s);

}  // namespace camera_iq
