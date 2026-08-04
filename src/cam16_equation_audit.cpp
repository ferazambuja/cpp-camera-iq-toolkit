#include "camera_iq/cam16_equation_audit.hpp"

#include <array>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>

#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

void require_j(double j) {
  if (!std::isfinite(j) || j < 0.0 || j > 100.0) {
    throw std::runtime_error("CAM16 equation audit: J must be within [0,100]");
  }
}

void require_relative_background(double y_background) {
  if (!std::isfinite(y_background) || y_background <= 0.0 ||
      y_background > 100.0) {
    throw std::runtime_error(
        "CAM16 equation audit: relative background must be within (0,100]");
  }
}

void require_r_squared(double value) {
  if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
    throw std::runtime_error(
        "CAM16 equation audit: published R-squared must be within [0,1]");
  }
}

}  // namespace

double cam16_normalized_brightness(double j) {
  require_j(j);
  return std::sqrt(j / 100.0);
}

double hellwig_2022_normalized_brightness(double j) {
  require_j(j);
  return j / 100.0;
}

double cam16_isolated_ncb_chroma_factor(double y_background,
                                        double reference_y_background) {
  require_relative_background(y_background);
  require_relative_background(reference_y_background);
  const double result =
      std::pow(reference_y_background / y_background, 0.18);
  if (!std::isfinite(result)) {
    throw std::runtime_error(
        "CAM16 equation audit: isolated Ncb factor overflow");
  }
  return result;
}

double cam16_relative_chroma_fixed_adapted_response(
    double y_background, double reference_j,
    double reference_y_background) {
  require_relative_background(y_background);
  require_relative_background(reference_y_background);
  if (!std::isfinite(reference_j) || reference_j <= 0.0 ||
      reference_j > 100.0) {
    throw std::runtime_error(
        "CAM16 equation audit: reference J must be within (0,100]");
  }

  const double n = y_background / 100.0;
  const double reference_n = reference_y_background / 100.0;
  const double z = 1.48 + std::sqrt(n);
  const double reference_z = 1.48 + std::sqrt(reference_n);
  const double isolated =
      std::pow(reference_n / n, 0.18);
  const double chroma_scale = std::pow(
      (1.64 - std::pow(0.29, n)) /
          (1.64 - std::pow(0.29, reference_n)),
      0.73);
  const double lightness_scale = std::pow(
      reference_j / 100.0,
      (z - reference_z) / (2.0 * reference_z));
  const double result = isolated * chroma_scale * lightness_scale;
  if (!std::isfinite(result)) {
    throw std::runtime_error(
        "CAM16 equation audit: coupled chroma expression overflow");
  }
  return result;
}

double hellwig_2022_colorfulness(double n_c, double eccentricity,
                                 double opponent_a, double opponent_b) {
  if (!std::isfinite(n_c) || n_c < 0.0 || !std::isfinite(eccentricity) ||
      eccentricity < 0.0 || !std::isfinite(opponent_a) ||
      !std::isfinite(opponent_b)) {
    throw std::runtime_error(
        "Hellwig 2022 Equation 23: inputs must be finite and non-negative where required");
  }
  const double result =
      43.0 * n_c * eccentricity * std::hypot(opponent_a, opponent_b);
  if (!std::isfinite(result)) {
    throw std::runtime_error("Hellwig 2022 Equation 23: result overflow");
  }
  return result;
}

Cam16EquationAuditReport build_cam16_equation_audit() {
  Cam16EquationAuditReport report;
  report.brightness_curve.reserve(21);
  for (int j = 0; j <= 100; j += 5) {
    const double value = static_cast<double>(j);
    report.brightness_curve.push_back(
        {value, cam16_normalized_brightness(value),
         hellwig_2022_normalized_brightness(value)});
  }
  constexpr std::array<double, 8> backgrounds = {
      20.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1};
  report.background_curve.reserve(backgrounds.size());
  for (const double y_background : backgrounds) {
    report.background_curve.push_back(
        {y_background,
         cam16_isolated_ncb_chroma_factor(y_background)});
    for (int reference_j = 10; reference_j <= 90; reference_j += 10) {
      report.coupled_chroma_curve.push_back(
          {y_background, static_cast<double>(reference_j),
           cam16_relative_chroma_fixed_adapted_response(
               y_background, static_cast<double>(reference_j))});
    }
  }
  report.performance = {0.86, 0.95, 0.87, 0.96, 0.81, 0.71};
  return report;
}

void write_cam16_equation_audit_json(
    std::ostream& os, const Cam16EquationAuditReport& report) {
  for (const double value : {
           report.performance.brightness_cam16_r_squared,
           report.performance.brightness_proposed_r_squared,
           report.performance.chroma_cam16_r_squared,
           report.performance.chroma_proposed_r_squared,
           report.performance.colorfulness_cam16_r_squared,
           report.performance.colorfulness_proposed_r_squared,
       }) {
    require_r_squared(value);
  }
  JsonWriter json(os);
  json.begin_object();
  json.key("schema_version");
  json.value(2);
  json.key("study");
  json.value("Hellwig_Fairchild_2022_CAM16_equation_audit");
  json.key("source_doi");
  json.value("10.1002/col.22792");
  json.key("scope");
  json.value("equation_level_not_perceptual_validation");
  json.key("equation_23_correction_date");
  json.value("2022-04-22");
  json.key("equation_23_coefficient");
  json.value(43);

  json.key("brightness");
  json.begin_object();
  json.key("contract");
  json.value("normalized_Q_over_Q_white_under_fixed_viewing_conditions");
  json.key("points");
  json.begin_array();
  for (const auto& point : report.brightness_curve) {
    json.begin_object();
    json.key("J");
    json.value(point.j);
    json.key("cam16_Q_over_Q_white");
    json.value(point.cam16_q_over_q_white);
    json.key("hellwig_2022_Q_over_Q_white");
    json.value(point.hellwig_2022_q_over_q_white);
    json.end_object();
  }
  json.end_array();
  json.end_object();

  json.key("complete_chroma_expression");
  json.begin_object();
  json.key("contract");
  json.value("fixed_adapted_response_reference_J_sweep");
  json.key("not_full_cam16_forward");
  json.value(true);
  json.key("relative_Y_white");
  json.value(100.0);
  json.key("reference_Y_b");
  json.value(20.0);
  json.key("reference_J_min");
  json.value(10.0);
  json.key("reference_J_max");
  json.value(90.0);
  json.key("reference_J_step");
  json.value(10.0);
  json.key("points");
  json.begin_array();
  for (const auto& point : report.coupled_chroma_curve) {
    json.begin_object();
    json.key("Y_b");
    json.value(point.y_background);
    json.key("reference_J");
    json.value(point.reference_j);
    json.key("relative_chroma");
    json.value(point.relative_chroma);
    json.end_object();
  }
  json.end_array();
  json.end_object();

  json.key("background_dependence");
  json.begin_object();
  json.key("contract");
  json.value("isolated_Ncb_to_the_0_9_contribution");
  json.key("not_full_cam16_chroma");
  json.value(true);
  json.key("reference_Y_b");
  json.value(20.0);
  json.key("points");
  json.begin_array();
  for (const auto& point : report.background_curve) {
    json.begin_object();
    json.key("Y_b");
    json.value(point.y_background);
    json.key("relative_factor");
    json.value(point.isolated_ncb_chroma_factor);
    json.end_object();
  }
  json.end_array();
  json.end_object();

  json.key("published_performance_R_squared");
  json.begin_object();
  json.key("brightness_CAM16");
  json.value(report.performance.brightness_cam16_r_squared);
  json.key("brightness_proposed");
  json.value(report.performance.brightness_proposed_r_squared);
  json.key("Munsell_chroma_CAM16");
  json.value(report.performance.chroma_cam16_r_squared);
  json.key("Munsell_chroma_proposed");
  json.value(report.performance.chroma_proposed_r_squared);
  json.key("LUTCHI_colorfulness_CAM16");
  json.value(report.performance.colorfulness_cam16_r_squared);
  json.key("LUTCHI_colorfulness_proposed");
  json.value(report.performance.colorfulness_proposed_r_squared);
  json.end_object();
  json.end_object();
}

void write_cam16_equation_audit_csv(
    std::ostream& os, const Cam16EquationAuditReport& report) {
  os << std::setprecision(15);
  os << "series,x,reference_j,value,comparison_value\n";
  for (const auto& point : report.brightness_curve) {
    os << "normalized_brightness," << point.j << ",,"
       << point.cam16_q_over_q_white << ','
       << point.hellwig_2022_q_over_q_white << '\n';
  }
  for (const auto& point : report.background_curve) {
    os << "isolated_ncb_factor," << point.y_background << ",,"
       << point.isolated_ncb_chroma_factor << ",\n";
  }
  for (const auto& point : report.coupled_chroma_curve) {
    os << "cam16_chroma_expression," << point.y_background << ','
       << point.reference_j << ',' << point.relative_chroma << ",\n";
  }
}

}  // namespace camera_iq
