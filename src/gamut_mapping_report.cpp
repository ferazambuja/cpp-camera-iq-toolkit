#include "camera_iq/gamut_mapping_report.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera_iq/csv.hpp"
#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

constexpr double kRadiansToDegrees =
    180.0 / 3.141592653589793238462643383279502884;

std::vector<std::string> parse_rfc4180_row(const std::string& line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  bool quote_closed = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          quoted = false;
          quote_closed = true;
        }
      } else {
        field += c;
      }
    } else if (c == ',' && !quoted) {
      fields.push_back(quote_closed ? field : trim_csv_cell(field));
      field.clear();
      quote_closed = false;
    } else if (c == '"' && field.empty() && !quote_closed) {
      quoted = true;
    } else {
      if (quote_closed && c != '\r') {
        throw std::runtime_error(
            "gamut CSV: characters after closing quote");
      }
      if (c != '\r') field += c;
    }
  }
  if (quoted) throw std::runtime_error("gamut CSV: unterminated quote");
  fields.push_back(quote_closed ? field : trim_csv_cell(field));
  return fields;
}

double parse_unit_component(const std::string& text, std::size_t row,
                            std::string_view channel) {
  const auto value = parse_double(text);
  if (!value || *value < 0.0 || *value > 1.0) {
    throw std::runtime_error("gamut CSV: row " + std::to_string(row) +
                             " " + std::string(channel) +
                             " must be finite and within [0,1]");
  }
  return *value;
}

double chroma(const Lab& lab) { return std::hypot(lab.a, lab.b); }

double hue_degrees(const Lab& lab) {
  if (chroma(lab) <= 1e-12) return 0.0;
  double degrees = std::atan2(lab.b, lab.a) * kRadiansToDegrees;
  if (degrees < 0.0) degrees += 360.0;
  return degrees;
}

double circular_delta_degrees(double output, double input) {
  double delta = std::fmod(output - input, 360.0);
  if (delta > 180.0) delta -= 360.0;
  if (delta < -180.0) delta += 360.0;
  return delta;
}

double ipt_hue_degrees(const Ipt& ipt) {
  if (std::hypot(ipt.p, ipt.t) <= 1e-12) return 0.0;
  double degrees = std::atan2(ipt.t, ipt.p) * kRadiansToDegrees;
  if (degrees < 0.0) degrees += 360.0;
  return degrees;
}

double linear_percentile(const std::vector<double>& sorted, double quantile) {
  if (sorted.empty()) return 0.0;
  const double position = quantile * static_cast<double>(sorted.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(lower);
  return sorted[lower] + fraction * (sorted[upper] - sorted[lower]);
}

double signed_unit_margin(const LinearRgb& rgb) {
  return std::min({rgb.r, rgb.g, rgb.b, 1.0 - rgb.r, 1.0 - rgb.g,
                   1.0 - rgb.b});
}

void validate_report_for_serialization(const GamutMapReport& report) {
  if (report.samples.empty()) {
    throw std::runtime_error(
        "gamut report: cannot serialize an empty sample set");
  }
  const auto coordinate_space =
      report.samples.front().mapping.mapping_coordinate_space;
  std::set<std::string> identifiers;
  for (const auto& sample : report.samples) {
    if (sample.id.empty() || !identifiers.insert(sample.id).second) {
      throw std::runtime_error(
          "gamut report: serialized sample IDs must be non-empty and unique");
    }
    if (sample.mapping.mapping_coordinate_space != coordinate_space) {
      throw std::runtime_error(
          "gamut report: serialized samples use mixed mapping coordinates");
    }
  }
}

std::string branch_name(GamutMapBranch branch) {
  switch (branch) {
    case GamutMapBranch::IdentityNoMappingRequired:
      return "identity_no_mapping_required";
    case GamutMapBranch::IdentityNoGamutContraction:
      return "identity_no_gamut_contraction";
    case GamutMapBranch::ProtectedCoreIdentity:
      return "protected_core_identity";
    case GamutMapBranch::FixedLhRadialBoundaryClip:
      return "fixed_Lh_radial_boundary_clip";
    case GamutMapBranch::FixedOklchRadialBoundaryClip:
      return "fixed_OkLCh_radial_boundary_clip";
    case GamutMapBranch::SoftChromaCompression:
      return "protected_core_asymptotic_soft_chroma_compression";
    case GamutMapBranch::CssColor4LocalMindeInitialClip:
      return "CSS_Color_4_local_MINDE_initial_clip";
    case GamutMapBranch::CssColor4LocalMindeBinarySearch:
      return "CSS_Color_4_local_MINDE_binary_search";
  }
  throw std::runtime_error("gamut report: unsupported mapping branch");
}

std::string csv_escape(std::string_view value) {
  if (value.find_first_of(",\"\r\n") == std::string_view::npos) {
    return std::string(value);
  }
  std::string out = "\"";
  for (const char c : value) {
    if (c == '"') out += '"';
    out += c;
  }
  out += '"';
  return out;
}

void json_rgb(JsonWriter& writer, const EncodedRgb& rgb) {
  writer.begin_array();
  writer.value(rgb.r);
  writer.value(rgb.g);
  writer.value(rgb.b);
  writer.end_array();
}

void json_linear_rgb(JsonWriter& writer, const LinearRgb& rgb) {
  writer.begin_array();
  writer.value(rgb.r);
  writer.value(rgb.g);
  writer.value(rgb.b);
  writer.end_array();
}

void json_xyz(JsonWriter& writer, const Xyz& xyz) {
  writer.begin_array();
  writer.value(xyz.x);
  writer.value(xyz.y);
  writer.value(xyz.z);
  writer.end_array();
}

void json_lab(JsonWriter& writer, const Lab& lab) {
  writer.begin_array();
  writer.value(lab.l);
  writer.value(lab.a);
  writer.value(lab.b);
  writer.end_array();
}

void json_oklab(JsonWriter& writer, const Oklab& oklab) {
  writer.begin_array();
  writer.value(oklab.l);
  writer.value(oklab.a);
  writer.value(oklab.b);
  writer.end_array();
}

void json_oklch(JsonWriter& writer, const Oklch& oklch) {
  writer.begin_object();
  writer.key("L");
  writer.value(oklch.l);
  writer.key("C");
  writer.value(oklch.c);
  writer.key("h_degrees");
  if (oklch.hue_defined) {
    writer.value(oklch.h_degrees);
  } else {
    writer.null();
  }
  writer.key("hue_defined");
  writer.value(oklch.hue_defined);
  writer.end_object();
}

void json_boundary_search(JsonWriter& writer,
                          const GamutBoundaryResult& boundary) {
  writer.begin_object();
  writer.key("chroma");
  writer.value(boundary.chroma);
  writer.key("lower_chroma");
  writer.value(boundary.lower_chroma);
  writer.key("upper_chroma");
  writer.value(boundary.upper_chroma);
  writer.key("bracket_width");
  writer.value(boundary.bracket_width);
  writer.key("segments_examined");
  writer.value(static_cast<std::int64_t>(boundary.segments_examined));
  writer.key("refinement_iterations");
  writer.value(static_cast<std::int64_t>(boundary.refinement_iterations));
  writer.key("converged");
  writer.value(boundary.converged);
  writer.end_object();
}

void json_matrix(JsonWriter& writer, RgbColorSpace space, bool to_xyz) {
  const std::array<LinearRgb, 3> basis =
      {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
  std::array<std::array<double, 3>, 3> matrix{};
  for (std::size_t column = 0; column < basis.size(); ++column) {
    if (to_xyz) {
      const Xyz value = linear_rgb_to_xyz(basis[column], space);
      matrix[0][column] = value.x;
      matrix[1][column] = value.y;
      matrix[2][column] = value.z;
    } else {
      const Xyz xyz{basis[column].r, basis[column].g, basis[column].b};
      const LinearRgb value = xyz_to_linear_rgb(xyz, space);
      matrix[0][column] = value.r;
      matrix[1][column] = value.g;
      matrix[2][column] = value.b;
    }
  }
  writer.begin_array();
  for (const auto& row : matrix) {
    writer.begin_array();
    for (const double value : row) writer.value(value);
    writer.end_array();
  }
  writer.end_array();
}

}  // namespace

std::string_view rgb_color_space_name(RgbColorSpace space) {
  switch (space) {
    case RgbColorSpace::Srgb:
      return "srgb";
    case RgbColorSpace::DisplayP3:
      return "display-p3";
  }
  throw std::runtime_error("gamut report: unsupported RGB color space");
}

std::string_view gamut_map_algorithm_name(GamutMapIntent intent) {
  switch (intent) {
    case GamutMapIntent::BoundaryProjection:
      return "fixed_Lh_radial_boundary_clip";
    case GamutMapIntent::SoftChromaCompression:
      return "experimental_CIELAB_protected_core_asymptotic_headroom_soft_chroma_compression";
    case GamutMapIntent::OklchBoundaryProjection:
      return "fixed_OkLCh_radial_boundary_clip";
    case GamutMapIntent::CssColor4LocalMinde:
      return "CSS_Color_4_2026-07-28_binary_search_local_MINDE";
  }
  throw std::runtime_error("gamut report: unsupported gamut-map intent");
}

std::string_view gamut_mapping_coordinate_space_name(
    GamutMappingCoordinateSpace space) {
  switch (space) {
    case GamutMappingCoordinateSpace::CielabD65:
      return "CIELAB_D65";
    case GamutMappingCoordinateSpace::OklabD65:
      return "OkLab_D65";
  }
  throw std::runtime_error(
      "gamut report: unsupported mapping coordinate space");
}

std::vector<GamutSampleInput> read_gamut_samples_csv(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("gamut CSV: cannot open " + path.string());
  }
  const std::string bytes{std::istreambuf_iterator<char>(input),
                          std::istreambuf_iterator<char>()};
  return parse_gamut_samples_csv(bytes);
}

std::vector<GamutSampleInput> parse_gamut_samples_csv(std::string_view bytes) {
  std::istringstream input{std::string(bytes)};
  std::string line;
  if (!std::getline(input, line)) {
    throw std::runtime_error("gamut CSV: empty file");
  }
  if (parse_rfc4180_row(line) !=
      std::vector<std::string>{"id", "r", "g", "b"}) {
    throw std::runtime_error("gamut CSV: expected header id,r,g,b");
  }

  std::vector<GamutSampleInput> out;
  std::set<std::string> ids;
  std::size_t row = 1;
  while (std::getline(input, line)) {
    ++row;
    if (trim_csv_cell(line).empty()) continue;
    const auto fields = parse_rfc4180_row(line);
    if (fields.size() != 4 || fields[0].empty()) {
      throw std::runtime_error("gamut CSV: row " + std::to_string(row) +
                               " must contain id,r,g,b");
    }
    if (!ids.insert(fields[0]).second) {
      throw std::runtime_error("gamut CSV: duplicate id " + fields[0]);
    }
    out.push_back({fields[0],
                   {parse_unit_component(fields[1], row, "r"),
                    parse_unit_component(fields[2], row, "g"),
                    parse_unit_component(fields[3], row, "b")}});
  }
  if (out.empty()) throw std::runtime_error("gamut CSV: no sample rows");
  return out;
}

GamutMapReport analyze_gamut_samples(
    const std::vector<GamutSampleInput>& samples,
    const GamutMapOptions& options, std::string_view input_label,
    std::string_view input_sha256) {
  if (samples.empty()) {
    throw std::runtime_error("gamut report: at least one sample is required");
  }
  if (!input_sha256.empty() &&
      (input_sha256.size() != 64 ||
       !std::all_of(input_sha256.begin(), input_sha256.end(),
                    [](unsigned char value) {
                      return std::isxdigit(value) != 0;
                    }))) {
    throw std::runtime_error(
        "gamut report: input SHA-256 must have 64 hexadecimal digits");
  }

  GamutMapReport report;
  report.options = options;
  report.input_label = input_label;
  report.input_sha256 = input_sha256;
  report.samples.reserve(samples.size());
  std::set<std::string> ids;
  double delta_e_sum = 0.0;
  double delta_e_ok_sum = 0.0;
  std::vector<double> modified_abs_ipt_hue_deltas;
  for (const auto& sample : samples) {
    if (sample.id.empty() || !ids.insert(sample.id).second) {
      throw std::runtime_error(
          "gamut report: sample IDs must be non-empty and unique");
    }
    GamutSampleReport item;
    item.id = sample.id;
    item.input_encoded = sample.encoded;
    item.mapping = map_encoded_rgb_to_gamut(sample.encoded, options);
    item.branch = branch_name(item.mapping.branch);
    item.input_hue_degrees = hue_degrees(item.mapping.input_lab);
    item.output_hue_degrees = hue_degrees(item.mapping.output_lab);
    item.delta_e_2000 =
        delta_e_2000(item.mapping.input_lab, item.mapping.output_lab);
    item.delta_e_ok =
        delta_e_ok(item.mapping.input_oklab, item.mapping.output_oklab);
    item.delta_lightness =
        item.mapping.output_lab.l - item.mapping.input_lab.l;
    item.delta_chroma =
        item.mapping.output_chroma - item.mapping.input_chroma;
    if (item.mapping.input_chroma > 1e-12 &&
        item.mapping.output_chroma > 1e-12) {
      item.delta_hue_degrees = circular_delta_degrees(
          item.output_hue_degrees, item.input_hue_degrees);
    }
    const Ipt input_ipt = xyz_d65_to_ipt(item.mapping.input_xyz);
    const Ipt output_ipt = xyz_d65_to_ipt(item.mapping.output_xyz);
    const auto hue_is_defined = [](const Ipt& ipt) {
      const double chroma = std::hypot(ipt.p, ipt.t);
      return chroma > 1e-12 &&
             chroma / std::max(std::abs(ipt.i), 1e-12) > 1e-3;
    };
    const bool input_ipt_hue_defined = hue_is_defined(input_ipt);
    const bool output_ipt_hue_defined = hue_is_defined(output_ipt);
    if (input_ipt_hue_defined) {
      item.input_ipt_hue_degrees = ipt_hue_degrees(input_ipt);
    }
    if (output_ipt_hue_defined) {
      item.output_ipt_hue_degrees = ipt_hue_degrees(output_ipt);
    }
    item.ipt_hue_defined =
        input_ipt_hue_defined && output_ipt_hue_defined;
    if (item.ipt_hue_defined) {
      item.delta_ipt_hue_degrees = circular_delta_degrees(
          item.output_ipt_hue_degrees, item.input_ipt_hue_degrees);
      if (item.mapping.modified) {
        modified_abs_ipt_hue_deltas.push_back(
            std::abs(item.delta_ipt_hue_degrees));
      }
    }
    item.destination_margin_before =
        signed_unit_margin(item.mapping.destination_linear_before);
    item.destination_margin_after =
        signed_unit_margin(item.mapping.destination_linear_after);
    if (item.mapping.boundary_evidence_applicable &&
        item.mapping.destination_boundary_chroma > 1e-12) {
      item.destination_boundary_utilization =
          item.mapping.output_mapping_chroma /
          item.mapping.destination_boundary_chroma;
    }

    if (!item.mapping.input_in_destination) ++report.out_of_gamut_count;
    if (item.mapping.modified) ++report.modified_count;
    delta_e_sum += item.delta_e_2000;
    delta_e_ok_sum += item.delta_e_ok;
    report.max_delta_e_2000 =
        std::max(report.max_delta_e_2000, item.delta_e_2000);
    report.max_delta_e_ok =
        std::max(report.max_delta_e_ok, item.delta_e_ok);
    report.max_abs_delta_lightness = std::max(
        report.max_abs_delta_lightness, std::abs(item.delta_lightness));
    report.max_abs_delta_hue_degrees = std::max(
        report.max_abs_delta_hue_degrees, std::abs(item.delta_hue_degrees));
    report.samples.push_back(std::move(item));
  }
  report.mean_delta_e_2000 = delta_e_sum / report.samples.size();
  report.mean_delta_e_ok = delta_e_ok_sum / report.samples.size();
  std::sort(modified_abs_ipt_hue_deltas.begin(),
            modified_abs_ipt_hue_deltas.end());
  report.ipt_hue_sample_count = modified_abs_ipt_hue_deltas.size();
  report.ipt_hue_above_3_degrees_count = static_cast<std::size_t>(
      std::count_if(modified_abs_ipt_hue_deltas.begin(),
                    modified_abs_ipt_hue_deltas.end(),
                    [](double value) { return value > 3.0; }));
  report.median_abs_delta_ipt_hue_degrees =
      linear_percentile(modified_abs_ipt_hue_deltas, 0.5);
  report.p90_abs_delta_ipt_hue_degrees =
      linear_percentile(modified_abs_ipt_hue_deltas, 0.9);
  if (!modified_abs_ipt_hue_deltas.empty()) {
    report.max_abs_delta_ipt_hue_degrees =
        modified_abs_ipt_hue_deltas.back();
  }
  return report;
}

void write_gamut_map_json(std::ostream& os, const GamutMapReport& report) {
  validate_report_for_serialization(report);
  JsonWriter writer(os);
  writer.begin_object();
  writer.key("schema_version");
  writer.value(kGamutMapSchemaVersion);
  writer.key("input_label");
  writer.value(report.input_label);
  writer.key("input_sha256");
  writer.value(report.input_sha256);
  writer.key("configuration");
  writer.begin_object();
  writer.key("algorithm");
  writer.value(gamut_map_algorithm_name(report.options.intent));
  writer.key("source");
  writer.value(rgb_color_space_name(report.options.source));
  writer.key("destination");
  writer.value(rgb_color_space_name(report.options.destination));
  writer.key("white");
  writer.value("D65");
  writer.key("white_xyz");
  json_xyz(writer, d65_white_xyz());
  writer.key("xyz_scale");
  writer.value("relative_Ywhite_1");
  writer.key("mapping_coordinate_space");
  writer.value(gamut_mapping_coordinate_space_name(
      report.samples.front().mapping.mapping_coordinate_space));
  writer.key("secondary_hue_diagnostic");
  writer.value("IPT_D65_hue_angle_difference");
  writer.key("secondary_hue_diagnostic_source");
  writer.value("Ebner_Fairchild_1998_signed_0.43_response");
  writer.key("relative_IPT_chroma_to_abs_I_threshold");
  writer.value(0.001);
  writer.key("chromatic_adaptation");
  writer.value("none_same_D65_white");
  writer.key("transfer_function");
  writer.value("sRGB_piecewise_for_sRGB_and_Display-P3");
  writer.key("transfer_parameters");
  writer.begin_object();
  writer.key("encoded_decode_breakpoint");
  writer.value(0.04045);
  writer.key("linear_encode_breakpoint");
  writer.value(0.0031308);
  writer.key("linear_segment_slope");
  writer.value(12.92);
  writer.key("power_offset");
  writer.value(0.055);
  writer.key("power_scale");
  writer.value(1.055);
  writer.key("power_exponent");
  writer.value(2.4);
  writer.end_object();
  writer.key("encoded_input_domain");
  writer.value("finite_[0,1]");
  writer.key("matrix_source");
  writer.value(
      "W3C_CSS_Color_4_2026-07-28_non_normative_sample_code");
  writer.key("source_linear_rgb_to_xyz");
  json_matrix(writer, report.options.source, true);
  writer.key("destination_xyz_to_linear_rgb");
  json_matrix(writer, report.options.destination, false);
  if (report.options.intent == GamutMapIntent::BoundaryProjection ||
      report.options.intent == GamutMapIntent::SoftChromaCompression) {
    writer.key("boundary_component");
    writer.value("first_transition_from_piecewise_cubic_channel_roots");
    writer.key("boundary_solver");
    writer.value("Lab_inverse_breakpoints_and_RGB_channel_surface_roots");
    writer.key("boundary_maximum_mapping_chroma");
    writer.value(report.options.boundary.maximum_chroma);
    writer.key("boundary_refinement_iterations");
    writer.value(static_cast<std::int64_t>(
        report.options.boundary.refinement_iterations));
    writer.key("boundary_chroma_tolerance");
    writer.value(report.options.boundary.chroma_tolerance);
    writer.key("gamut_tolerance");
    writer.value(report.options.boundary.gamut_tolerance);
  } else if (report.options.intent ==
             GamutMapIntent::OklchBoundaryProjection) {
    writer.key("boundary_component");
    writer.value("first_transition_from_cubic_RGB_channel_roots");
    writer.key("boundary_solver");
    writer.value("OkLab_inverse_cubic_RGB_channel_surface_roots");
    writer.key("boundary_maximum_mapping_chroma");
    writer.value(report.options.oklch_boundary.maximum_chroma);
    writer.key("boundary_refinement_iterations");
    writer.value(static_cast<std::int64_t>(
        report.options.oklch_boundary.refinement_iterations));
    writer.key("boundary_chroma_tolerance");
    writer.value(report.options.oklch_boundary.chroma_tolerance);
    writer.key("gamut_tolerance");
    writer.value(report.options.oklch_boundary.gamut_tolerance);
  } else if (report.options.intent == GamutMapIntent::CssColor4LocalMinde) {
    writer.key("css_color_4_revision");
    writer.value("2026-07-28");
    writer.key("specification_status");
    writer.value("W3C_Candidate_Recommendation_Draft_work_in_progress");
    writer.key("JND_delta_e_ok");
    writer.value(0.02);
    writer.key("binary_search_epsilon");
    writer.value(0.0001);
    writer.key("clip_rule");
    writer.value("convert_to_destination_then_clamp_each_RGB_component");
    writer.key("intent_class");
    writer.value("relative_colorimetric_individual_SDR_colors");
    writer.key("gamut_tolerance");
    writer.value(report.options.oklch_boundary.gamut_tolerance);
  }
  if (report.options.intent == GamutMapIntent::SoftChromaCompression) {
    writer.key("knee_fraction_of_destination_boundary");
    writer.value(report.options.knee_fraction);
    writer.key("soft_curve");
    writer.value("K+(D-K)*(C-K)/((D-K)+(C-K))");
    writer.key("soft_curve_boundary_behavior");
    writer.value("asymptotic_headroom_for_all_finite_input_chroma");
    writer.key("source_boundary_role");
    writer.value("reported_not_used_by_asymptotic_curve");
    writer.key("soft_intent_status");
    writer.value("experimental_CIELAB_baseline");
  }
  writer.end_object();

  writer.key("aggregate");
  writer.begin_object();
  writer.key("sample_count");
  writer.value(static_cast<std::int64_t>(report.samples.size()));
  writer.key("out_of_gamut_count");
  writer.value(static_cast<std::int64_t>(report.out_of_gamut_count));
  writer.key("modified_count");
  writer.value(static_cast<std::int64_t>(report.modified_count));
  writer.key("mean_delta_e_2000");
  writer.value(report.mean_delta_e_2000);
  writer.key("max_delta_e_2000");
  writer.value(report.max_delta_e_2000);
  writer.key("mean_delta_e_ok");
  writer.value(report.mean_delta_e_ok);
  writer.key("max_delta_e_ok");
  writer.value(report.max_delta_e_ok);
  writer.key("max_abs_delta_Lstar");
  writer.value(report.max_abs_delta_lightness);
  writer.key("max_abs_delta_Lab_hue_degrees");
  writer.value(report.max_abs_delta_hue_degrees);
  writer.key("IPT_hue_diagnostic");
  writer.begin_object();
  writer.key("modified_chromatic_sample_count");
  writer.value(static_cast<std::int64_t>(report.ipt_hue_sample_count));
  writer.key("median_abs_delta_degrees");
  writer.value(report.median_abs_delta_ipt_hue_degrees);
  writer.key("p90_abs_delta_degrees");
  writer.value(report.p90_abs_delta_ipt_hue_degrees);
  writer.key("p90_method");
  writer.value("linear_interpolation_q_times_n_minus_1");
  writer.key("max_abs_delta_degrees");
  writer.value(report.max_abs_delta_ipt_hue_degrees);
  writer.key("count_abs_delta_above_3_degrees");
  writer.value(static_cast<std::int64_t>(
      report.ipt_hue_above_3_degrees_count));
  writer.key("interpretation");
  writer.value("coordinate_diagnostic_not_observer_validation");
  writer.end_object();
  writer.end_object();

  writer.key("samples");
  writer.begin_array();
  for (const auto& sample : report.samples) {
    const auto& mapping = sample.mapping;
    writer.begin_object();
    writer.key("id");
    writer.value(sample.id);
    writer.key("input_encoded_rgb");
    json_rgb(writer, sample.input_encoded);
    writer.key("input_xyz_D65");
    json_xyz(writer, mapping.input_xyz);
    writer.key("input_Lab_D65");
    json_lab(writer, mapping.input_lab);
    writer.key("input_OkLab_D65");
    json_oklab(writer, mapping.input_oklab);
    writer.key("input_OkLCh_D65");
    json_oklch(writer, mapping.input_oklch);
    writer.key("destination_linear_before");
    json_linear_rgb(writer, mapping.destination_linear_before);
    writer.key("destination_margin_before");
    writer.value(sample.destination_margin_before);
    writer.key("input_in_destination");
    writer.value(mapping.input_in_destination);
    writer.key("modified");
    writer.value(mapping.modified);
    writer.key("branch");
    writer.value(sample.branch);
    writer.key("mapping_coordinates");
    writer.begin_object();
    writer.key("space");
    writer.value(gamut_mapping_coordinate_space_name(
        mapping.mapping_coordinate_space));
    writer.key("input_chroma");
    writer.value(mapping.input_mapping_chroma);
    writer.key("output_chroma");
    writer.value(mapping.output_mapping_chroma);
    writer.end_object();
    writer.key("boundary_evidence");
    writer.begin_object();
    writer.key("applicable");
    writer.value(mapping.boundary_evidence_applicable);
    if (mapping.boundary_evidence_applicable) {
      writer.key("source_connected_boundary_mapping_chroma");
      writer.value(mapping.source_boundary_chroma);
      writer.key("source_boundary_search");
      json_boundary_search(writer, mapping.source_boundary);
      writer.key("destination_connected_boundary_mapping_chroma");
      writer.value(mapping.destination_boundary_chroma);
      writer.key("destination_boundary_search");
      json_boundary_search(writer, mapping.destination_boundary);
      writer.key("knee_mapping_chroma");
      writer.value(mapping.knee_chroma);
      writer.key("destination_boundary_utilization");
      writer.value(sample.destination_boundary_utilization);
    }
    writer.end_object();
    writer.key("local_minde");
    writer.begin_object();
    writer.key("applicable");
    writer.value(mapping.local_minde.applicable);
    if (mapping.local_minde.applicable) {
      writer.key("JND_delta_e_ok");
      writer.value(mapping.local_minde.jnd);
      writer.key("binary_search_epsilon");
      writer.value(mapping.local_minde.epsilon);
      writer.key("iterations");
      writer.value(static_cast<std::int64_t>(
          mapping.local_minde.iterations));
      writer.key("final_delta_e_ok");
      writer.value(mapping.local_minde.final_delta_e_ok);
      writer.key("returned_clipped_color");
      writer.value(mapping.local_minde.returned_clipped_color);
    }
    writer.end_object();
    writer.key("output_Lab_D65");
    json_lab(writer, mapping.output_lab);
    writer.key("output_OkLab_D65");
    json_oklab(writer, mapping.output_oklab);
    writer.key("output_OkLCh_D65");
    json_oklch(writer, mapping.output_oklch);
    writer.key("output_xyz_D65");
    json_xyz(writer, mapping.output_xyz);
    writer.key("destination_linear_after_unclamped");
    json_linear_rgb(writer, mapping.destination_linear_after);
    writer.key("destination_margin_after");
    writer.value(sample.destination_margin_after);
    writer.key("output_in_destination");
    writer.value(mapping.output_in_destination);
    writer.key("output_encoded_rgb");
    json_rgb(writer, mapping.output_encoded);
    writer.key("delta_e_2000");
    writer.value(sample.delta_e_2000);
    writer.key("delta_e_ok");
    writer.value(sample.delta_e_ok);
    writer.key("delta_Lstar");
    writer.value(sample.delta_lightness);
    writer.key("delta_Cstar");
    writer.value(sample.delta_chroma);
    writer.key("delta_Lab_hue_degrees");
    writer.value(sample.delta_hue_degrees);
    writer.key("input_IPT_hue_degrees");
    writer.value(sample.input_ipt_hue_degrees);
    writer.key("output_IPT_hue_degrees");
    writer.value(sample.output_ipt_hue_degrees);
    writer.key("delta_IPT_hue_degrees");
    writer.value(sample.delta_ipt_hue_degrees);
    writer.key("IPT_hue_defined");
    writer.value(sample.ipt_hue_defined);
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
}

void write_gamut_map_csv(std::ostream& os, const GamutMapReport& report) {
  validate_report_for_serialization(report);
  os << "id,input_r,input_g,input_b,input_in_destination,modified,branch,"
        "output_r,output_g,output_b,mapping_coordinate_space,"
        "input_Lstar,input_Cstar,input_Lab_hue_degrees,"
        "output_Lstar,output_Cstar,output_Lab_hue_degrees,"
        "input_OkLab_L,input_OkLab_a,input_OkLab_b,input_OkLCh_C,input_OkLCh_h_degrees,input_OkLCh_hue_defined,"
        "output_OkLab_L,output_OkLab_a,output_OkLab_b,output_OkLCh_C,output_OkLCh_h_degrees,output_OkLCh_hue_defined,"
        "input_mapping_chroma,output_mapping_chroma,boundary_evidence_applicable,"
        "source_connected_boundary_mapping_chroma,destination_connected_boundary_mapping_chroma,knee_mapping_chroma,"
        "destination_boundary_utilization,local_minde_applicable,local_minde_iterations,local_minde_final_delta_e_ok,local_minde_returned_clipped_color,"
        "delta_e_2000,delta_e_ok,delta_Lstar,delta_Cstar,delta_Lab_hue_degrees,"
        "input_IPT_hue_degrees,output_IPT_hue_degrees,delta_IPT_hue_degrees,IPT_hue_defined,"
        "destination_margin_before,destination_margin_after,"
        "output_in_destination\n";
  os << std::setprecision(17);
  for (const auto& sample : report.samples) {
    const auto& map = sample.mapping;
    os << csv_escape(sample.id) << ',' << sample.input_encoded.r << ','
       << sample.input_encoded.g << ',' << sample.input_encoded.b << ','
       << (map.input_in_destination ? "true" : "false") << ','
       << (map.modified ? "true" : "false") << ','
       << csv_escape(sample.branch) << ',' << map.output_encoded.r << ','
       << map.output_encoded.g << ',' << map.output_encoded.b << ','
       << gamut_mapping_coordinate_space_name(map.mapping_coordinate_space)
       << ','
       << map.input_lab.l << ',' << map.input_chroma << ','
       << sample.input_hue_degrees << ',' << map.output_lab.l << ','
       << map.output_chroma << ',' << sample.output_hue_degrees << ','
       << map.input_oklab.l << ',' << map.input_oklab.a << ','
       << map.input_oklab.b << ',' << map.input_oklch.c << ','
       << map.input_oklch.h_degrees << ','
       << (map.input_oklch.hue_defined ? "true" : "false") << ','
       << map.output_oklab.l << ',' << map.output_oklab.a << ','
       << map.output_oklab.b << ',' << map.output_oklch.c << ','
       << map.output_oklch.h_degrees << ','
       << (map.output_oklch.hue_defined ? "true" : "false") << ','
       << map.input_mapping_chroma << ',' << map.output_mapping_chroma << ','
       << (map.boundary_evidence_applicable ? "true" : "false") << ','
       << map.source_boundary_chroma << ',' << map.destination_boundary_chroma
       << ',' << map.knee_chroma << ','
       << sample.destination_boundary_utilization << ','
       << (map.local_minde.applicable ? "true" : "false") << ','
       << map.local_minde.iterations << ','
       << map.local_minde.final_delta_e_ok << ','
       << (map.local_minde.returned_clipped_color ? "true" : "false") << ','
       << sample.delta_e_2000 << ',' << sample.delta_e_ok << ','
       << sample.delta_lightness << ',' << sample.delta_chroma << ','
       << sample.delta_hue_degrees << ',' << sample.input_ipt_hue_degrees
       << ',' << sample.output_ipt_hue_degrees << ','
       << sample.delta_ipt_hue_degrees << ','
       << (sample.ipt_hue_defined ? "true" : "false") << ','
       << sample.destination_margin_before
       << ',' << sample.destination_margin_after << ','
       << (map.output_in_destination ? "true" : "false") << '\n';
  }
}

}  // namespace camera_iq
