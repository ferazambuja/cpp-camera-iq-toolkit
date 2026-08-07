#include "camera_iq/spectral_series.hpp"

#include <limits>
#include <stdexcept>
#include <string>

#include "harness.hpp"

using camera_iq::read_spectral_series_csv;
using test::check;

namespace {

template <typename Operation>
bool throws_with(Operation operation, const std::string& needle) {
  try {
    operation();
  } catch (const std::runtime_error& error) {
    return std::string(error.what()).find(needle) != std::string::npos;
  }
  return false;
}
}  // namespace

void TESTS() {
  const std::string valid =
      "series_id,reading_id,wavelength_nm,value\n"
      "reference,r1,380,1\nreference,r1,390,2\n"
      "reference,r2,380,2\nreference,r2,390,4\n"
      "candidate,c1,380,3\ncandidate,c1,390,6\n";
  const auto series = read_spectral_series_csv(valid);
  check(series.size() == 2 && series[0].id == "reference" &&
            series[1].id == "candidate",
        "spectral series: first-seen series order is retained");
  check(series[0].reading_ids == std::vector<std::string>({"r1", "r2"}),
        "spectral series: repeat identities are retained");
  check(series[0].readings[1].values == std::vector<double>({2.0, 4.0}),
        "spectral series: samples are grouped by repeat identity");

  check(throws_with(
            [] {
              (void)read_spectral_series_csv(
                  "series,reading,wavelength,value\na,b,1,2\n");
            },
            "unexpected header"),
        "spectral series: schema is exact");
  check(throws_with(
            [] {
              (void)read_spectral_series_csv(
                  "series_id,reading_id,wavelength_nm,value\n"
                  "a,b,1,2\na,b,1,3\n");
            },
            "duplicate wavelength"),
        "spectral series: duplicate samples are refused");
  check(throws_with(
            [] {
              (void)read_spectral_series_csv(
                  "series_id,reading_id,wavelength_nm,value\n"
                  "a,b,1,2\na,b,3,2\na,b,4,2\n");
            },
            "uniform grid"),
        "spectral series: non-uniform axes are refused");
  check(throws_with(
            [] {
              (void)read_spectral_series_csv(
                  "series_id,reading_id,wavelength_nm,value\n"
                  "a,b,1,2\na,b,2,2\na,c,1,2\na,c,3,2\n");
            },
            "share one wavelength grid"),
        "spectral series: repeat-axis mismatch is refused");
  check(throws_with(
            [] {
              (void)read_spectral_series_csv(
                  "series_id,reading_id,wavelength_nm,value\n"
                  "a,b,1,nan\na,b,2,2\n");
            },
            "finite number"),
        "spectral series: non-finite samples are refused");
  check(throws_with(
            [] {
              (void)read_spectral_series_csv(
                  "series_id,reading_id,wavelength_nm,value\n");
            },
            "no samples"),
        "spectral series: empty data is refused");
}
