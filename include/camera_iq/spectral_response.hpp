#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

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
  std::vector<int> axis_nm;
  std::vector<double> response_r;
  std::vector<double> response_g;
  std::vector<double> response_b;
  std::vector<double> line_spd;
  std::string normalization;
  std::string validation_tier;
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

void write_spectral_response_json(std::ostream& os,
                                  const SpectralResponse& response);

}  // namespace camera_iq
