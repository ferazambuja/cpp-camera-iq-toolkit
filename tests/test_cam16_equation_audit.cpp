#include "camera_iq/cam16_equation_audit.hpp"

#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "harness.hpp"

using camera_iq::build_cam16_equation_audit;
using camera_iq::cam16_isolated_ncb_chroma_factor;
using camera_iq::cam16_relative_chroma_fixed_adapted_response;
using camera_iq::cam16_normalized_brightness;
using camera_iq::hellwig_2022_colorfulness;
using camera_iq::hellwig_2022_normalized_brightness;
using camera_iq::write_cam16_equation_audit_csv;
using camera_iq::write_cam16_equation_audit_json;
using test::check;
using test::check_near;

void TESTS() {
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_normalized_brightness(25.0), 0.5, 1e-15,
                 "CAM16 audit: half normalized brightness occurs at J=25"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_normalized_brightness(50.0), std::sqrt(0.5), 1e-15,
                 "CAM16 audit: middle lightness is not middle brightness"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(hellwig_2022_normalized_brightness(50.0), 0.5, 1e-15,
                 "Hellwig audit: proposed normalized brightness is linear in J"));

  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_isolated_ncb_chroma_factor(20.0), 1.0, 1e-15,
                 "CAM16 audit: mid-gray background is the factor reference"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_isolated_ncb_chroma_factor(5.0),
                 1.2834258975629043, 1e-15,
                 "CAM16 audit: isolated Ncb factor at Yb=5"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_isolated_ncb_chroma_factor(1.0),
                 1.7146891477615709, 1e-15,
                 "CAM16 audit: isolated Ncb factor at Yb=1"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_isolated_ncb_chroma_factor(0.1),
                 2.595287047166021, 1e-15,
                 "CAM16 audit: isolated Ncb factor at Yb=0.1"));

  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_relative_chroma_fixed_adapted_response(20.0, 10.0),
                 1.0, 1e-15,
                 "CAM16 audit: coupled expression is normalized at Yb=20"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_relative_chroma_fixed_adapted_response(0.1, 10.0),
                 2.6865933941337503, 1e-12,
                 "CAM16 audit: low-J coupled expression can exceed isolated Ncb"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(cam16_relative_chroma_fixed_adapted_response(0.1, 90.0),
                 2.1198928552563943, 1e-12,
                 "CAM16 audit: high-J coupled expression can fall below isolated Ncb"));
  CAMERA_IQ_DOC_EVIDENCE(
      color_model_audit_numeric_oracles,
      check_near(hellwig_2022_colorfulness(1.0, 1.0, 3.0, 4.0), 215.0,
                 1e-12,
                 "Hellwig audit: corrected Equation 23 uses coefficient 43"));

  bool zero_background_threw = false;
  try {
    (void)cam16_isolated_ncb_chroma_factor(0.0);
  } catch (const std::runtime_error&) {
    zero_background_threw = true;
  }
  check(zero_background_threw,
        "CAM16 audit: zero background is an asymptote, not a finite sample");

  // Both brightness relations are defined on a normalized scale where J=100 is
  // the reference white, so a J outside [0,100] has no meaning. Neither
  // function traps it downstream -- J=150 returns a finite value above unity
  // from both -- so the domain guard is the only thing preventing a silently
  // meaningless result, and the J=25 versus J=50 midpoint comparison depends
  // on that scale being bounded.
  const auto brightness_rejected = [](double j) {
    bool cam16_threw = false;
    bool hellwig_threw = false;
    try {
      (void)cam16_normalized_brightness(j);
    } catch (const std::runtime_error&) {
      cam16_threw = true;
    }
    try {
      (void)hellwig_2022_normalized_brightness(j);
    } catch (const std::runtime_error&) {
      hellwig_threw = true;
    }
    return cam16_threw && hellwig_threw;
  };
  check(brightness_rejected(100.0 + 1e-9),
        "CAM16 audit: both brightness relations reject J just above 100");
  check(brightness_rejected(150.0),
        "CAM16 audit: both brightness relations reject J above 100");
  check(brightness_rejected(-1e-9),
        "CAM16 audit: both brightness relations reject negative J");
  check(brightness_rejected(std::numeric_limits<double>::quiet_NaN()),
        "CAM16 audit: both brightness relations reject NaN J");
  check(brightness_rejected(std::numeric_limits<double>::infinity()),
        "CAM16 audit: both brightness relations reject infinite J");

  const auto isolated_background_rejected = [](double y_background,
                                                double reference_background) {
    try {
      (void)cam16_isolated_ncb_chroma_factor(y_background,
                                             reference_background);
    } catch (const std::runtime_error&) {
      return true;
    }
    return false;
  };
  check(isolated_background_rejected(101.0, 20.0),
        "CAM16 audit: isolated factor rejects Yb above 100");
  check(isolated_background_rejected(20.0, 101.0),
        "CAM16 audit: isolated factor rejects reference Yb above 100");
  check(isolated_background_rejected(
            std::numeric_limits<double>::quiet_NaN(), 20.0),
        "CAM16 audit: isolated factor rejects NaN background");
  check(isolated_background_rejected(
            std::numeric_limits<double>::infinity(), 20.0),
        "CAM16 audit: isolated factor rejects infinite background");

  const auto report = build_cam16_equation_audit();
  check(report.brightness_curve.size() == 21,
        "CAM16 audit: brightness curve sample count is pinned");
  check(report.background_curve.size() == 8,
        "CAM16 audit: background curve sample count is pinned");
  check(report.coupled_chroma_curve.size() == 72,
        "CAM16 audit: nine reference-J curves cover eight backgrounds");
  check_near(report.performance.brightness_cam16_r_squared, 0.86, 0.0,
             "CAM16 audit: published brightness CAM16 R-squared");
  check_near(report.performance.brightness_proposed_r_squared, 0.95, 0.0,
             "CAM16 audit: published brightness proposed R-squared");
  check_near(report.performance.chroma_cam16_r_squared, 0.87, 0.0,
             "CAM16 audit: published Munsell chroma CAM16 R-squared");
  check_near(report.performance.chroma_proposed_r_squared, 0.96, 0.0,
             "CAM16 audit: published Munsell chroma proposed R-squared");
  check_near(report.performance.colorfulness_cam16_r_squared, 0.81, 0.0,
             "CAM16 audit: published colorfulness CAM16 R-squared");
  check_near(report.performance.colorfulness_proposed_r_squared, 0.71, 0.0,
             "CAM16 audit: published colorfulness tradeoff is retained");

  std::ostringstream json;
  write_cam16_equation_audit_json(json, report);
  check(json.str().find("\"schema_version\":2") != std::string::npos,
        "CAM16 audit: JSON schema version is typed");
  check(json.str().find("equation_level_not_perceptual_validation") !=
            std::string::npos,
        "CAM16 audit: JSON scope boundary is machine-readable");
  check(json.str().find("isolated_Ncb_to_the_0_9_contribution") !=
            std::string::npos,
        "CAM16 audit: isolated factor is not serialized as full chroma");
  check(json.str().find("fixed_adapted_response_reference_J_sweep") !=
            std::string::npos,
        "CAM16 audit: coupled-expression contract is machine-readable");

  auto invalid_report = report;
  invalid_report.performance.chroma_proposed_r_squared = 1.01;
  bool invalid_performance_threw = false;
  try {
    std::ostringstream invalid_json;
    write_cam16_equation_audit_json(invalid_json, invalid_report);
  } catch (const std::runtime_error&) {
    invalid_performance_threw = true;
  }
  check(invalid_performance_threw,
        "CAM16 audit: writer rejects R-squared outside [0,1]");

  std::ostringstream csv;
  write_cam16_equation_audit_csv(csv, report);
  check(csv.str().find(
            "series,x,reference_j,value,comparison_value\n") == 0,
        "CAM16 audit: CSV schema is pinned");
  check(csv.str().find("isolated_ncb_factor,0.1,,2.59528704716602") !=
            std::string::npos,
        "CAM16 audit: CSV carries the isolated-factor value");
  check(csv.str().find("cam16_chroma_expression,0.1,10,2.68659339413375") !=
            std::string::npos,
        "CAM16 audit: CSV carries the low-J coupled-expression value");
}
