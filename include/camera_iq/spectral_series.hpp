#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/sampled_spectrum.hpp"

namespace camera_iq {

struct SpectralSeries {
  std::string id;
  std::vector<std::string> reading_ids;
  std::vector<SampledSpectrum> readings;
};

// Canonical long-form schema:
// series_id,reading_id,wavelength_nm,value
// Rows are grouped by the two identifiers after parsing; first-seen order is
// preserved. Every reading must carry one complete uniform axis and every
// reading in a series must share that axis.
std::vector<SpectralSeries> read_spectral_series_csv(std::string_view csv_text);
std::vector<SpectralSeries> read_spectral_series_csv_file(
    const std::filesystem::path& path);

}  // namespace camera_iq
