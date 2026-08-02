#include "camera_iq/spectro_colorimetry.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include "harness.hpp"

using camera_iq::compute_spectro_closure;
using camera_iq::read_spectro_cmf_csv;
using camera_iq::SpectroMeasurement;
using test::check;
using test::check_near;

namespace {

SpectroMeasurement reading(double scale) {
  SpectroMeasurement value;
  value.wavelength_nm = {1.0, 2.0};
  value.spectral_radiance = {2.0 * scale, 3.0 * scale};
  value.recorded_xyz = {20.0 * scale, 30.0 * scale, 50.0 * scale};
  return value;
}

} // namespace

void TESTS() {
  const auto observer = read_spectro_cmf_csv("Wavelength (nm),X,Y,Z\n"
                                             "1,1,0,1\n"
                                             "2,0,1,1\n");
  const auto closure =
      compute_spectro_closure({reading(1.0), reading(2.0)}, observer);
  check(closure.sample_weighting == "uniform_equal_weight" &&
            closure.scale_source == "derived_from_recorded_xyz",
        "spectro closure: weighting and scale source are explicit");
  check_near(closure.scale_value, 10.0, 1e-12,
             "spectro closure: one global proportional scale is fitted");
  check(closure.readings.size() == 2 &&
            closure.readings[0].computed_xyz ==
                std::array<double, 3>({20.0, 30.0, 50.0}),
        "spectro closure: equal-weight XYZ is recomputed from the spectrum");
  check_near(closure.max_absolute_relative_residual_percent, 0.0, 1e-12,
             "spectro closure: an exact synthetic relationship closes");

  const auto high_range =
      compute_spectro_closure({reading(1.0e200)}, observer);
  check(std::isfinite(high_range.scale_value) &&
            std::fabs(high_range.scale_value - 10.0) <=
                4.0 * std::numeric_limits<double>::epsilon() * 10.0,
        "spectro closure: a representable fit survives squared-product "
        "overflow");
  check(std::isfinite(high_range.max_absolute_relative_residual_percent),
        "spectro closure: high-range residuals remain finite");

  const auto cancellation_observer = read_spectro_cmf_csv(
      "Wavelength (nm),X,Y,Z\n"
      "1,1,1,1\n"
      "2,1,1,1\n"
      "3,1,1,1\n");
  SpectroMeasurement cancellation = reading(1.0);
  cancellation.wavelength_nm = {1.0, 2.0, 3.0};
  const double three_quarters_max =
      0.75 * std::numeric_limits<double>::max();
  cancellation.spectral_radiance = {
      three_quarters_max, three_quarters_max, -three_quarters_max};
  cancellation.recorded_xyz = {
      three_quarters_max, three_quarters_max, three_quarters_max};
  const auto cancellation_closure =
      compute_spectro_closure({cancellation}, cancellation_observer);
  check(cancellation_closure.readings[0].computed_xyz ==
            cancellation.recorded_xyz &&
            cancellation_closure.max_absolute_relative_residual_percent ==
                0.0,
        "spectro closure: a representable tristimulus survives intermediate "
        "summation overflow");

  bool mismatch_threw = false;
  try {
    SpectroMeasurement shifted = reading(1.0);
    shifted.wavelength_nm[1] = 3.0;
    (void)compute_spectro_closure({shifted}, observer);
  } catch (const std::runtime_error &) {
    mismatch_threw = true;
  }
  check(mismatch_threw,
        "spectro closure: observer and measurement axes must match exactly");

  bool duplicate_wavelength_threw = false;
  try {
    (void)read_spectro_cmf_csv("Wavelength (nm),X,Y,Z\n1,1,0,0\n1,0,1,0\n");
  } catch (const std::runtime_error &) {
    duplicate_wavelength_threw = true;
  }
  check(duplicate_wavelength_threw,
        "spectro closure: a non-increasing observer grid is refused");

  const auto crlf = read_spectro_cmf_csv("Wavelength (nm),X,Y,Z\r\n"
                                         "1,1,0,1\r\n"
                                         "2,0,1,1\r\n");
  check(crlf.wavelength_nm.size() == 2,
        "spectro closure: the committed CMF line endings are accepted");
}
