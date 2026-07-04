#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/exposure_response.hpp"

namespace camera_iq {

struct OecfFitSample {
  double shutter_s = 0.0;
  std::string shutter_str;
  double relative_exposure = 0.0;
  double signal = 0.0;
  double fitted_signal = 0.0;
  double residual = 0.0;
};

struct OecfPlaneFit {
  std::string channel;
  std::size_t n_points = 0;
  double slope = 0.0;
  double intercept = 0.0;
  double r_squared = 0.0;
  double max_nonlinearity_pct = 0.0;
  std::vector<OecfFitSample> points;
};

struct OecfSeriesFit {
  ExposureSeries series;
  bool exif_consistent = true;
  bool oecf_candidate = false;
  bool fit_candidate = false;
  std::size_t usable_oecf_points = 0;
  std::array<std::optional<OecfPlaneFit>, 4> plane_fits;
  std::vector<std::string> limitations;
};

OecfSeriesFit fit_oecf_series(const ExposureResponseSummary& summary);

void write_oecf_fit_json(std::ostream& os, std::string_view root_label,
                         const std::vector<OecfSeriesFit>& fits);

}  // namespace camera_iq
