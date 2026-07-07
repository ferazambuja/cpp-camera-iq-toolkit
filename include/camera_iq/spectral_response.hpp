#pragma once

#include <filesystem>
#include <iosfwd>
#include <array>
#include <string>
#include <vector>

#include "camera_iq/raw_meta.hpp"
#include "camera_iq/roi.hpp"

namespace camera_iq {

struct SpectralResponseProvenance {
  std::string camera_model;
  std::string dataset_id;
  std::string archive_subset;
};

struct SpectralResponse {
  std::string camera_model;
  std::string dataset_id;
  std::string archive_subset;
  std::string source;
  std::vector<int> axis_nm;
  std::vector<double> response_r;
  std::vector<double> response_g;
  std::vector<double> response_b;
  std::vector<double> line_spd;
  std::string normalization;
  std::string validation_tier;
};

struct SpectralFidelityChannel {
  double rms = 0;
  double correlation = 0;
};

struct SpectralFidelityComparison {
  std::string validation_tier = "legacy_fidelity_only";
  SpectralFidelityChannel r;
  SpectralFidelityChannel g;
  SpectralFidelityChannel b;
};

struct SpectralSampleDiagnostics {
  int wavelength_nm = 0;
  std::filesystem::path raw_path;
  double mean_signal_r = 0;
  double mean_signal_g = 0;
  double mean_signal_b = 0;
  double saturated_fraction_r = 0;
  double saturated_fraction_g = 0;
  double saturated_fraction_b = 0;
  double below_dark_fraction_r = 0;
  double below_dark_fraction_g = 0;
  double below_dark_fraction_b = 0;
};

struct SpectralRawExtraction {
  SpectralResponse response;
  std::array<double, 4> dark_residual_mean_by_position{0, 0, 0, 0};
  std::array<double, 4> metadata_black_by_position{0, 0, 0, 0};
  RoiRect measurement_roi;
  double near_saturation_fraction = 0.98;
  std::vector<SpectralSampleDiagnostics> samples;
  SpectralFidelityComparison tier1_legacy_fidelity;
};

// Parses legacy Gold legacy monochromator outputs and the corresponding line-SPD
// sidecar. The returned response is a fidelity reference only; it is not a
// toolkit-derived camera spectral sensitivity function.
SpectralResponse parse_spectral_response(
    const std::filesystem::path& legacy_response_csv,
    const std::filesystem::path& line_spd_csv,
    SpectralResponseProvenance provenance = {});

// Records the legacy normalization convention without rescaling the values.
SpectralResponse normalize_spectral_response(SpectralResponse response);

// Returns the sorted, contiguous RAW sweep files that map one-to-one to the
// validated wavelength axis. The mapping is positional and therefore rejects
// missing or extra files before any RAW extraction runs.
std::vector<std::filesystem::path> discover_spectral_sweep_files(
    const std::filesystem::path& raw_dir, const std::vector<int>& axis_nm);

// Builds a toolkit-derived response from already-unpacked, post-LibRaw
// black-subtracted CFA images. Dark-frame residual means are subtracted per CFA
// position, so Canon post-unpack cblack handling stays in the raw_meta path.
SpectralRawExtraction extract_raw_spectral_response(
    const SpectralResponse& legacy,
    const std::vector<RawCfaImage>& sweep_images, const RawCfaImage& dark_image,
    RoiRect requested_roi, double near_saturation_fraction = 0.98,
    std::vector<std::filesystem::path> raw_paths = {});

// Convenience wrapper for the real RAW path.
SpectralRawExtraction extract_raw_spectral_response_from_files(
    const SpectralResponse& legacy, const std::filesystem::path& raw_dir,
    const std::filesystem::path& dark_raw, RoiRect requested_roi,
    double near_saturation_fraction = 0.98);

void write_spectral_response_json(std::ostream& os,
                                  const SpectralResponse& response);

void write_spectral_raw_extraction_json(
    std::ostream& os, const SpectralResponse& legacy,
    const SpectralRawExtraction& extraction);

}  // namespace camera_iq
