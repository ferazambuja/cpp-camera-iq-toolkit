#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/spectro_colorimetry.hpp"

namespace camera_iq {

struct SpectralReferenceColorimetryAudit {
  std::string illuminant;
  std::string observer;
  std::string integration = "trapezoidal_on_declared_grid";
  bool observer_metadata_conflict = false;
  std::vector<std::string> source_observer_declarations;
  std::size_t lab_patch_count = 0;
  double mean_delta_e_76 = 0.0;
  double max_delta_e_76 = 0.0;
  std::size_t xyz_patch_count = 0;
  double mean_xyz_relative_l2 = 0.0;
  double max_xyz_relative_l2 = 0.0;
};

struct SpectralReferenceRepeatPatch {
  std::string id;
  double reflectance_rms = 0.0;
  double delta_e_76 = 0.0;
};

struct SpectralReferenceRepeatAudit {
  std::string illuminant;
  std::string observer;
  std::string evidence_scope = "candidate_paired_series_observed_variation";
  std::size_t patch_count = 0;
  double mean_reflectance_rms = 0.0;
  double max_reflectance_rms = 0.0;
  double mean_delta_e_76 = 0.0;
  double max_delta_e_76 = 0.0;
  std::vector<SpectralReferenceRepeatPatch> patches;
};

// Recomputes colorimetry from reflectance with caller-selected tables. Source
// metadata is evidence to report, never an implicit observer-selection rule.
SpectralReferenceColorimetryAudit audit_spectral_reference_colorimetry(
    const SpectralReference& reference,
    const std::vector<double>& illuminant,
    const SpectroCmfTable& observer,
    std::string illuminant_label,
    std::string observer_label);

// Compares two retained chart spectra in declared row order. In the absence of
// instrument/session metadata the result is observed variation of a candidate
// paired series, not a general repeatability or instrument-accuracy estimate.
SpectralReferenceRepeatAudit audit_spectral_reference_repeat(
    const SpectralReference& first,
    const SpectralReference& second,
    const std::vector<double>& illuminant,
    const SpectroCmfTable& observer,
    std::string illuminant_label,
    std::string observer_label);

}  // namespace camera_iq
