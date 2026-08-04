#include "camera_iq/gamut_mapping_report.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "harness.hpp"

using camera_iq::GamutMapIntent;
using camera_iq::GamutMapOptions;
using camera_iq::RgbColorSpace;
using camera_iq::analyze_gamut_samples;
using camera_iq::read_gamut_samples_csv;
using camera_iq::write_gamut_map_csv;
using camera_iq::write_gamut_map_json;
using test::check;
using test::check_near;

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, const std::string& contents) {
  std::ofstream os(path, std::ios::binary);
  os << contents;
}

}  // namespace

void TESTS() {
  const fs::path root =
      fs::temp_directory_path() / "camera_iq_gamut_mapping_report";
  fs::remove_all(root);
  fs::create_directories(root);

  const fs::path input = root / "synthetic.csv";
  write_file(input,
             "id,r,g,b\n"
             "p3_red,1,0,0\n"
             "gray,0.5,0.5,0.5\n"
             "\"yellow, corner\",1,1,0\n");
  const auto samples = read_gamut_samples_csv(input);
  check(samples.size() == 3, "gamut CSV: three samples parsed");
  check(samples[2].id == "yellow, corner",
        "gamut CSV: quoted identifier parsed");

  GamutMapOptions options;
  options.source = RgbColorSpace::DisplayP3;
  options.destination = RgbColorSpace::Srgb;
  options.intent = GamutMapIntent::BoundaryProjection;
  const auto report = analyze_gamut_samples(
      samples, options, "synthetic.csv",
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  check(report.samples.size() == 3, "gamut report: three sample results");
  check(report.out_of_gamut_count == 2,
        "gamut report: P3 red and yellow are out of sRGB");
  check(report.modified_count == 2,
        "gamut report: radial clip modifies two samples");
  check(report.samples[0].mapping.output_in_destination,
        "gamut report: accepted samples carry the hard postcondition");
  check(report.max_delta_e_2000 > 0.0,
        "gamut report: displacement metric is populated");
  check(report.ipt_hue_sample_count == 2,
        "gamut report: IPT hue diagnostic covers modified chromatic samples");
  const double red_abs_ipt =
      std::abs(report.samples[0].delta_ipt_hue_degrees);
  const double yellow_abs_ipt =
      std::abs(report.samples[2].delta_ipt_hue_degrees);
  const double ipt_low = std::min(red_abs_ipt, yellow_abs_ipt);
  const double ipt_high = std::max(red_abs_ipt, yellow_abs_ipt);
  check_near(report.median_abs_delta_ipt_hue_degrees,
             0.5 * (ipt_low + ipt_high), 1e-14,
             "gamut report: IPT median uses modified samples");
  check_near(report.p90_abs_delta_ipt_hue_degrees,
             ipt_low + 0.9 * (ipt_high - ipt_low), 1e-14,
             "gamut report: IPT p90 uses type-7 interpolation");
  check_near(report.max_abs_delta_ipt_hue_degrees, ipt_high, 0.0,
             "gamut report: IPT maximum uses the diagnostic tail");
  check(yellow_abs_ipt > red_abs_ipt,
        "gamut report: P3 yellow is the fixture's IPT-hue worst case");
  check(report.ipt_hue_above_3_degrees_count == 1,
        "gamut report: IPT tail count distinguishes full red from yellow");
  check(!report.samples[1].ipt_hue_defined,
        "gamut report: neutral IPT hue is explicitly undefined");
  check_near(report.samples[1].input_ipt_hue_degrees, 0.0, 0.0,
             "gamut report: undefined neutral IPT input hue is not serialized as an angle");
  check_near(report.samples[1].output_ipt_hue_degrees, 0.0, 0.0,
             "gamut report: undefined neutral IPT output hue is not serialized as an angle");

  std::ostringstream json;
  write_gamut_map_json(json, report);
  const std::string json_text = json.str();
  check(json_text.find("\"schema_version\":3") != std::string::npos,
        "gamut JSON: schema pinned");
  check(json_text.find("\"algorithm\":\"fixed_Lh_radial_boundary_clip\"") !=
            std::string::npos,
        "gamut JSON: intent is scientifically specific");
  check(json_text.find("\"xyz_scale\":\"relative_Ywhite_1\"") !=
            std::string::npos,
        "gamut JSON: XYZ scale explicit");
  check(json_text.find("\"mapping_coordinate_space\":\"CIELAB_D65\"") !=
            std::string::npos,
        "gamut JSON: CIELAB radial coordinates are explicit");
  check(json_text.find("\"white_xyz\":[0.950455") != std::string::npos,
        "gamut JSON: numeric D65 white serialized");
  check(json_text.find("\"encoded_decode_breakpoint\":0.04045") !=
            std::string::npos,
        "gamut JSON: numeric transfer breakpoint serialized");
  check(json_text.find("\"power_exponent\":2.4") != std::string::npos,
        "gamut JSON: numeric transfer exponent serialized");
  check(json_text.find(
            "\"matrix_source\":\"W3C_CSS_Color_4_2026-07-28_non_normative_sample_code\"") !=
            std::string::npos,
        "gamut JSON: dated matrix source does not overstate authority");
  check(json_text.find("matrix_authority") == std::string::npos,
        "gamut JSON: non-normative sample code is not called an authority");
  check(json_text.find("\"chromatic_adaptation\":\"none_same_D65_white\"") !=
            std::string::npos,
        "gamut JSON: adaptation decision explicit");
  check(json_text.find("\"secondary_hue_diagnostic\":\"IPT_D65_hue_angle_difference\"") !=
            std::string::npos,
        "gamut JSON: IPT is labeled as a secondary diagnostic");
  check(json_text.find("\"relative_IPT_chroma_to_abs_I_threshold\":0.001") !=
            std::string::npos,
        "gamut JSON: IPT hue validity threshold explicit");
  check(json_text.find("\"p90_method\":\"linear_interpolation_q_times_n_minus_1\"") !=
            std::string::npos,
        "gamut JSON: IPT percentile convention explicit");
  check(json_text.find("\"input_sha256\":\"aaaaaaaa") !=
            std::string::npos,
        "gamut JSON: input digest present");
  check(json_text.find("yellow, corner") != std::string::npos,
        "gamut JSON: sample ID present");
  check(json_text.find("\"delta_e_2000\":null") == std::string::npos &&
            json_text.find("\"delta_e_ok\":null") == std::string::npos &&
            json_text.find("\"destination_margin_after\":null") ==
                std::string::npos,
        "gamut JSON: computed numeric evidence is finite");
  check(json_text.find("soft_curve") == std::string::npos,
        "gamut JSON: radial intent omits inapplicable soft configuration");
  check(json_text.find("boundary_violation_count") == std::string::npos,
        "gamut JSON: hard invariant is not published as a vacuous count");
  check(json_text.find("\"destination_boundary_search\":{") !=
            std::string::npos,
        "gamut JSON: per-sample boundary solver evidence emitted");
  check(json_text.find("\"segments_examined\":") != std::string::npos,
        "gamut JSON: root-interval search work emitted");
  check(json_text.find("\"delta_IPT_hue_degrees\":") != std::string::npos,
        "gamut JSON: per-sample IPT hue diagnostic emitted");
  check(json_text.find("\"IPT_hue_defined\":false") != std::string::npos,
        "gamut JSON: undefined neutral IPT hue is machine-readable");

  std::ostringstream csv;
  write_gamut_map_csv(csv, report);
  const std::string csv_text = csv.str();
  check(csv_text.find("id,input_r,input_g,input_b") == 0,
        "gamut CSV: stable header");
  check(csv_text.find("delta_IPT_hue_degrees") != std::string::npos,
        "gamut CSV: IPT hue diagnostic is machine-readable");
  check(csv_text.find("IPT_hue_defined") != std::string::npos,
        "gamut CSV: IPT hue validity is machine-readable");
  check(csv_text.find("mapping_coordinate_space") != std::string::npos,
        "gamut CSV: mapping coordinate space is machine-readable");
  check(csv_text.find("delta_e_ok") != std::string::npos,
        "gamut CSV: common OkLab displacement diagnostic is emitted");
  check(csv_text.find("\"yellow, corner\"") != std::string::npos,
        "gamut CSV: identifier is RFC-escaped");
  check(csv_text.find(",false,true,") != std::string::npos,
        "gamut CSV: in-gamut and modified flags emitted");

  const fs::path duplicate = root / "duplicate.csv";
  write_file(duplicate, "id,r,g,b\na,0,0,0\na,1,1,1\n");
  bool duplicate_threw = false;
  try {
    (void)read_gamut_samples_csv(duplicate);
  } catch (const std::runtime_error&) {
    duplicate_threw = true;
  }
  check(duplicate_threw, "gamut CSV: duplicate IDs rejected");

  const fs::path bad_domain = root / "bad-domain.csv";
  write_file(bad_domain, "id,r,g,b\na,1.1,0,0\n");
  bool domain_threw = false;
  try {
    (void)read_gamut_samples_csv(bad_domain);
  } catch (const std::runtime_error&) {
    domain_threw = true;
  }
  check(domain_threw, "gamut CSV: encoded values outside [0,1] rejected");

  const fs::path bad_header = root / "bad-header.csv";
  write_file(bad_header, "name,r,g,b\na,1,0,0\n");
  bool header_threw = false;
  try {
    (void)read_gamut_samples_csv(bad_header);
  } catch (const std::runtime_error&) {
    header_threw = true;
  }
  check(header_threw, "gamut CSV: exact schema required");

  bool digest_threw = false;
  try {
    (void)analyze_gamut_samples(samples, options, "synthetic.csv",
                                std::string(64, 'z'));
  } catch (const std::runtime_error&) {
    digest_threw = true;
  }
  check(digest_threw, "gamut report: SHA-256 must be hexadecimal");

  GamutMapOptions oklch_options = options;
  oklch_options.intent = GamutMapIntent::OklchBoundaryProjection;
  const auto oklch_report = analyze_gamut_samples(
      samples, oklch_options, "synthetic.csv",
      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
  std::ostringstream oklch_json;
  write_gamut_map_json(oklch_json, oklch_report);
  check(oklch_json.str().find(
            "\"algorithm\":\"fixed_OkLCh_radial_boundary_clip\"") !=
            std::string::npos,
        "gamut JSON: OkLCh radial algorithm is distinct from coordinate space");
  check(oklch_json.str().find(
            "\"mapping_coordinate_space\":\"OkLab_D65\"") !=
            std::string::npos,
        "gamut JSON: OkLCh radial coordinate space is explicit");

  GamutMapOptions css_options = options;
  css_options.intent = GamutMapIntent::CssColor4LocalMinde;
  const auto css_report = analyze_gamut_samples(
      samples, css_options, "synthetic.csv",
      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
  std::ostringstream css_json;
  write_gamut_map_json(css_json, css_report);
  const std::string css_text = css_json.str();
  check(css_text.find(
            "\"algorithm\":\"CSS_Color_4_2026-07-28_binary_search_local_MINDE\"") !=
            std::string::npos,
        "gamut JSON: dated CSS algorithm is named exactly");
  check(css_text.find("\"css_color_4_revision\":\"2026-07-28\"") !=
            std::string::npos,
        "gamut JSON: draft revision is serialized");
  check(css_text.find("\"JND_delta_e_ok\":0.02") != std::string::npos,
        "gamut JSON: Local MINDE JND is serialized");
  check(css_text.find("\"binary_search_epsilon\":1e-04") !=
            std::string::npos,
        "gamut JSON: Local MINDE epsilon is serialized");
  check(css_text.find("\"boundary_evidence\":{\"applicable\":false}") !=
            std::string::npos,
        "gamut JSON: inapplicable radial-boundary evidence is explicit");
  check(css_text.find("\"local_minde\":{\"applicable\":true") !=
            std::string::npos,
        "gamut JSON: per-sample Local MINDE evidence is typed");

  camera_iq::GamutMapReport empty_report;
  bool empty_json_threw = false;
  try {
    std::ostringstream empty_json;
    write_gamut_map_json(empty_json, empty_report);
  } catch (const std::runtime_error&) {
    empty_json_threw = true;
  }
  check(empty_json_threw,
        "gamut JSON: empty public reports are rejected before serialization");

  bool empty_csv_threw = false;
  try {
    std::ostringstream empty_csv;
    write_gamut_map_csv(empty_csv, empty_report);
  } catch (const std::runtime_error&) {
    empty_csv_threw = true;
  }
  check(empty_csv_threw,
        "gamut CSV: empty public reports are rejected before serialization");

  auto mixed_coordinates = report;
  mixed_coordinates.samples.back().mapping.mapping_coordinate_space =
      camera_iq::GamutMappingCoordinateSpace::OklabD65;
  bool mixed_coordinates_threw = false;
  try {
    std::ostringstream mixed_json;
    write_gamut_map_json(mixed_json, mixed_coordinates);
  } catch (const std::runtime_error&) {
    mixed_coordinates_threw = true;
  }
  check(mixed_coordinates_threw,
        "gamut JSON: one global coordinate label cannot hide mixed samples");

  fs::remove_all(root);
}
