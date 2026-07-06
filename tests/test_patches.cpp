#include "camera_iq/patches.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "camera_iq/demosaic.hpp"
#include "camera_iq/raw_meta.hpp"
#include "harness.hpp"

using camera_iq::CameraRgbPatch;
using camera_iq::FlatFieldCorrectionSummary;
using camera_iq::PatchGeometryReport;
using camera_iq::PatchGeometryReportPatch;
using camera_iq::PatchGeometryReportPoint;
using camera_iq::PatchChannelComparison;
using camera_iq::PatchCoord;
using camera_iq::PatchLocalizationValidationThresholds;
using camera_iq::PatchMean;
using camera_iq::RawMeta;
using camera_iq::RawDiggerPatchTable;
using camera_iq::SpectralReferenceOrientationReport;
using camera_iq::SpectralReferenceOrientationScore;
using camera_iq::SpectralReferencePairing;
using camera_iq::WhiteBalanceGains;
using camera_iq::apply_flat_field;
using camera_iq::apply_white_balance;
using camera_iq::compare_patch_means_to_rgb;
using camera_iq::extract_patch_means;
using camera_iq::flat_field_near_ceiling_threshold_fraction;
using camera_iq::flat_field_normalization_policy;
using camera_iq::read_patch_coords_csv;
using camera_iq::read_rawdigger_patch_table;
using camera_iq::validate_patch_localization_against_oracle;
using camera_iq::write_camera_rgb_csv;
using camera_iq::write_patch_report_json;
using camera_iq::white_balance_gains_from_flat_field;
using test::check;
using test::check_near;

void TESTS() {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "camera_iq_patches";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  {
    std::ofstream os(root / "coord.csv", std::ios::binary);
    os << "1,1,2,2\n"
       << "3,2,2,3\n"
       << "5,5,4,4\n";
  }

  const auto coords = read_patch_coords_csv(root / "coord.csv");
  check(coords.size() == 3, "coords: three rows parsed");
  check_near(coords[0].x, 1.0, 1e-12, "coords: x parsed");
  check_near(coords[0].height, 2.0, 1e-12, "coords: height parsed");

  std::vector<camera_iq::RgbPixel> image(25);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      const double base = y * 10 + x;
      image[static_cast<std::size_t>(y * 5 + x)] =
          {base, 100.0 + base, 200.0 + base};
    }
  }

  const auto patches = extract_patch_means(image, 5, 5, coords);
  check(patches.size() == 3, "extract: three patches");
  check(patches[0].sample_count == 4, "extract: first sample count");
  check_near(patches[0].rgb.r, 5.5, 1e-12,
             "extract: one-based first ROI mean");
  check_near(patches[0].rgb.g, 105.5, 1e-12,
             "extract: green channel mean");
  check(patches[1].sample_count == 6, "extract: rectangular sample count");
  check_near(patches[1].rgb.r, 22.5, 1e-12,
             "extract: rectangular ROI mean");
  check(patches[2].sample_count == 1, "extract: clipped sample count");
  check_near(patches[2].rgb.b, 244.0, 1e-12,
             "extract: clipped ROI mean");

  const std::vector<CameraRgbPatch> target = {
      {2.0 * patches[0].rgb.r + 1.0, -patches[0].rgb.g + 3.0,
       0.5 * patches[0].rgb.b - 7.0},
      {2.0 * patches[1].rgb.r + 1.0, -patches[1].rgb.g + 3.0,
       0.5 * patches[1].rgb.b - 7.0},
      {2.0 * patches[2].rgb.r + 1.0, -patches[2].rgb.g + 3.0,
       0.5 * patches[2].rgb.b - 7.0},
  };
  const auto comparison = compare_patch_means_to_rgb(patches, target);
  check(comparison.patch_count == 3, "compare: patch count");
  check_near(comparison.channels[0].correlation, 1.0, 1e-12,
             "compare: red correlation");
  check_near(comparison.channels[0].slope, 2.0, 1e-12,
             "compare: red slope");
  check_near(comparison.channels[0].intercept, 1.0, 1e-12,
             "compare: red intercept");
  check_near(comparison.channels[0].mean_error_before_affine, -25.0, 1e-12,
             "compare: red direct bias");
  check_near(comparison.channels[0].rmse_before_affine, 29.5493936768, 1e-9,
             "compare: red direct rmse");
  check_near(comparison.channels[0].max_abs_error_before_affine, 45.0, 1e-12,
             "compare: red direct max abs error");
  check_near(comparison.channels[1].correlation, -1.0, 1e-12,
             "compare: green negative correlation");
  check_near(comparison.channels[1].rmse_after_affine, 0.0, 1e-12,
             "compare: affine rmse");

  RawDiggerPatchTable oracle;
  oracle.coords = {PatchCoord{1, 1, 2, 2}, PatchCoord{5, 1, 2, 2},
                   PatchCoord{9, 1, 2, 2}};
  oracle.reference_rgb = {{10, 20, 30}, {20, 40, 60}, {30, 60, 90}};
  PatchLocalizationValidationThresholds localization_thresholds;
  localization_thresholds.expected_patch_count = 3;
  localization_thresholds.max_center_error_px = 5.0;
  localization_thresholds.min_channel_correlation = 0.999;
  localization_thresholds.max_abs_mean_error_dn = 25.0;

  std::vector<PatchMean> localized = {
      PatchMean{PatchCoord{1, 1, 2, 2}, 0, 0, 2, 2, 4, {10, 20, 30}},
      PatchMean{PatchCoord{5, 1, 2, 2}, 4, 0, 2, 2, 4, {20, 40, 60}},
      PatchMean{PatchCoord{9, 1, 2, 2}, 8, 0, 2, 2, 4, {30, 60, 90}},
  };
  auto localization = validate_patch_localization_against_oracle(
      localized, oracle, localization_thresholds);
  check(localization.passes,
        "localization validation: matching oracle passes all gates");
  check(localization.center_residuals.size() == 3,
        "localization validation: center residuals emitted per patch");
  check(localization.center_residuals[0].reference_patch_id == "A1",
        "localization validation: first residual carries reference id");
  check(localization.center_residuals[1].reference_patch_id == "B1",
        "localization validation: second residual carries reference id");
  // Pin the row/column ints. They are computed by separate index helpers than
  // reference_patch_id (which derives row/col inline), and the report's
  // column-bow diagnostic groups residuals by residual.column. Indices 0..2 all
  // live in row 0, so these catch a row/column SWAP (a swap puts row==1 at
  // index 1) but NOT a wrong row stride (/10 vs /14) — every index below 10
  // maps to row 0 under either divisor. The stride is pinned by the
  // boundary-crossing fixture below (index 13 and 14).
  check(localization.center_residuals[0].row == 0 &&
            localization.center_residuals[0].column == 0,
        "localization validation: residual row/column map index 0 to A1 cell");
  check(localization.center_residuals[1].row == 0 &&
            localization.center_residuals[1].column == 1,
        "localization validation: residual row/column map index 1 to B1 cell");
  check(localization.center_residuals[2].row == 0 &&
            localization.center_residuals[2].column == 2,
        "localization validation: residual row/column map index 2 to C1 cell");
  check_near(localization.center_residuals[2].generated_center_x, 9.0, 1e-12,
             "localization validation: generated center x recorded");
  check_near(localization.center_residuals[2].oracle_center_x, 9.0, 1e-12,
             "localization validation: oracle center x recorded");
  check_near(localization.max_center_error_px, 0.0, 1e-12,
             "localization validation: max center error");
  check(localization.patch_count_gate_passes,
        "localization validation: patch-count gate");
  check(localization.correlation_gate_passes,
        "localization validation: correlation gate");
  check(localization.mean_error_gate_passes,
        "localization validation: absolute mean gate");

  // Row-stride pin: cross the SG 14-column boundary so the divisor itself is
  // constrained. Index 13 is the last cell of row 0 (N1); index 14 is the first
  // of row 1 (A2). A /10 stride would put index 13 at row 1 / column 3 (D2) and
  // index 14 at column 4 (E2), so these assertions fail loudly on a wrong
  // divisor that indices 0..2 cannot see. Labels are index-derived, so gate
  // pass/fail is irrelevant here; a matching 15-patch grid keeps it simple.
  RawDiggerPatchTable wide_oracle;
  std::vector<PatchMean> wide_localized;
  for (int i = 0; i < 15; ++i) {
    const PatchCoord coord{1.0 + i, 1.0, 2.0, 2.0};
    const CameraRgbPatch rgb{10.0 + i, 20.0 + 2.0 * i, 30.0 + 3.0 * i};
    wide_oracle.coords.push_back(coord);
    wide_oracle.reference_rgb.push_back(rgb);
    wide_localized.push_back(PatchMean{coord, i, 0, 2, 2, 4, rgb});
  }
  PatchLocalizationValidationThresholds wide_thresholds;
  wide_thresholds.expected_patch_count = 15;
  const auto wide = validate_patch_localization_against_oracle(
      wide_localized, wide_oracle, wide_thresholds);
  check(wide.center_residuals.size() == 15,
        "localization validation: wide fixture emits 15 residuals");
  check(wide.center_residuals[13].reference_patch_id == "N1" &&
            wide.center_residuals[13].row == 0 &&
            wide.center_residuals[13].column == 13,
        "localization validation: index 13 is row 0 column 13 (N1), not /10");
  check(wide.center_residuals[14].reference_patch_id == "A2" &&
            wide.center_residuals[14].row == 1 &&
            wide.center_residuals[14].column == 0,
        "localization validation: index 14 crosses to row 1 column 0 (A2)");

  std::vector<PatchMean> shifted = localized;
  for (auto& patch : shifted) {
    patch.source_coord.x += 6.0;
  }
  localization = validate_patch_localization_against_oracle(
      shifted, oracle, localization_thresholds);
  check(!localization.passes,
        "localization validation: shifted grid fails despite perfect RGB");
  check_near(localization.center_residuals[0].dx_px, 6.0, 1e-12,
             "localization validation: residual dx records shifted grid");
  check_near(localization.center_residuals[0].dy_px, 0.0, 1e-12,
             "localization validation: residual dy records shifted grid");
  check_near(localization.center_residuals[0].distance_px, 6.0, 1e-12,
             "localization validation: residual distance records shifted grid");
  check(!localization.center_gate_passes,
        "localization validation: shifted grid fails center gate");
  check(localization.correlation_gate_passes,
        "localization validation: shifted grid still passes correlation");

  std::vector<PatchMean> offset = localized;
  for (auto& patch : offset) {
    patch.rgb.r += 30.0;
    patch.rgb.g += 30.0;
    patch.rgb.b += 30.0;
  }
  localization = validate_patch_localization_against_oracle(
      offset, oracle, localization_thresholds);
  check(!localization.passes,
        "localization validation: DN offset fails despite high correlation");
  check(localization.correlation_gate_passes,
        "localization validation: DN offset still passes correlation");
  check(!localization.mean_error_gate_passes,
        "localization validation: DN offset fails absolute mean gate");

  const std::vector<camera_iq::RgbPixel> flat_image = {
      {10, 100, 1000},
      {20, 200, 2000},
      {0.25, 300, 3000},
      {30, 400, 4000},
  };
  const std::vector<camera_iq::RgbPixel> chart_image = {
      {100, 200, 300},
      {100, 200, 300},
      {100, 200, 300},
      {100, 200, 300},
  };
  FlatFieldCorrectionSummary flat_summary;
  const auto flat_corrected =
      apply_flat_field(chart_image, flat_image, 2, 2, 1.0, &flat_summary);
  check_near(flat_summary.normalizer.r, 20.0, 1e-12,
             "flat: red normalizer is valid-pixel mean, not max");
  check_near(flat_summary.normalizer.g, 250.0, 1e-12,
             "flat: green normalizer");
  check(flat_summary.clamped_sample_count == 1,
        "flat: low denominator counted");
  check_near(flat_corrected[0].r, 200.0, 1e-12,
             "flat: divides by local red flat");
  check_near(flat_corrected[1].r, 100.0, 1e-12,
             "flat: bright flat reduces signal");
  check_near(flat_corrected[2].r, 2000.0, 1e-12,
             "flat: floor protects low flat values");

  const auto flat_wb = white_balance_gains_from_flat_field(flat_summary);
  check_near(flat_wb.r, 12.5, 1e-12, "wb: flat-derived red gain");
  check_near(flat_wb.g, 1.0, 1e-12, "wb: flat-derived green anchor");
  check_near(flat_wb.b, 0.1, 1e-12, "wb: flat-derived blue gain");

  check(flat_field_normalization_policy() ==
            "per_channel_mean_valid_samples",
        "flat report: normalization policy constant");
  check_near(flat_field_near_ceiling_threshold_fraction(), 0.98, 1e-12,
             "flat report: near-ceiling threshold constant");
  flat_summary.near_ceiling_sample_count = 0;
  flat_summary.near_ceiling_fraction = 0.0;
  flat_summary.max_allowed_near_ceiling_fraction = 0.01;
  RawMeta meta;
  meta.make = "Fixture";
  meta.model = "Synthetic";
  meta.cfa_pattern = "RGGB";
  meta.black_level = 1024.0;
  meta.white_level = 16383.0;
  PatchMean report_patch;
  report_patch.source_coord = PatchCoord{1, 2, 3, 4};
  report_patch.x = 0;
  report_patch.y = 1;
  report_patch.width = 3;
  report_patch.height = 4;
  report_patch.sample_count = 12;
  report_patch.rgb = CameraRgbPatch{1, 2, 3};
  std::ostringstream patch_json;
  write_patch_report_json(
      patch_json, "dataset:fixture/raw.RAF", "dataset:fixture/coords.csv",
      "rawdigger_csv_zero_based_left_top", meta, 2, 2,
      "dataset:fixture/flat.RAF", flat_summary, flat_wb,
      "flat_field_green_anchor", {report_patch}, {"A1"}, std::nullopt, "");
  const std::string patch_doc = patch_json.str();
  check(patch_doc.find("\"normalization\":\"per_channel_mean_valid_samples\"") !=
            std::string::npos,
        "flat report: JSON records normalization policy");
  check(patch_doc.find("\"near_ceiling_threshold_fraction\":0.98") !=
            std::string::npos,
        "flat report: JSON records near-ceiling threshold");
  check(patch_doc.find("\"white_balance\":{\"policy\":\"flat_field_green_anchor\"") !=
            std::string::npos,
        "flat report: JSON records flat-derived WB policy");

  PatchGeometryReport geometry;
  geometry.chart_model = "ColorChecker-SG 14x10";
  geometry.method = "corner_seeded_projective_grid";
  geometry.corners = {PatchGeometryReportPoint{0, 0},
                      PatchGeometryReportPoint{242, 0},
                      PatchGeometryReportPoint{242, 172},
                      PatchGeometryReportPoint{0, 172}};
  geometry.patches = {PatchGeometryReportPatch{"A1", 0, 0}};
  std::ostringstream geometry_json;
  write_patch_report_json(
      geometry_json, "dataset:fixture/raw.RAF", "manual:sg-corners",
      "colorchecker_sg_corner_seeded_projective_grid", meta, 2, 2, "",
      std::nullopt, std::nullopt, "none", {report_patch}, {"A1"},
      std::nullopt, "", geometry);
  const std::string geometry_doc = geometry_json.str();
  check(geometry_doc.find("\"generated_chart_geometry\"") !=
            std::string::npos,
        "chart report: generated chart geometry block emitted");
  check(geometry_doc.find("\"chart_model\":\"ColorChecker-SG 14x10\"") !=
            std::string::npos,
        "chart report: chart model emitted");
  check(geometry_doc.find("\"corner_order\":[\"top_left\",\"top_right\","
                          "\"bottom_right\",\"bottom_left\"]") !=
            std::string::npos,
        "chart report: corner order emitted");
  check(geometry_doc.find("\"reference_patch_id\":\"A1\"") !=
            std::string::npos,
        "chart report: reference patch id emitted");
  check(geometry_doc.find("\"row\":0") != std::string::npos &&
            geometry_doc.find("\"column\":0") != std::string::npos,
        "chart report: row and column emitted");

  SpectralReferencePairing direct_pairing;
  direct_pairing.patch_count = 140;
  direct_pairing.luminance_correlation = 0.958;
  direct_pairing.red_green_correlation = 0.960;
  direct_pairing.blue_green_correlation = 0.961;
  direct_pairing.thresholds.min_luminance_correlation = 0.90;
  direct_pairing.thresholds.min_red_green_correlation = 0.80;
  direct_pairing.thresholds.min_blue_green_correlation = 0.80;
  direct_pairing.passes = true;
  SpectralReferencePairing column_pairing = direct_pairing;
  column_pairing.luminance_correlation = 0.327;
  column_pairing.passes = false;
  SpectralReferenceOrientationReport orientation;
  orientation.best_orientation = "direct";
  orientation.orientation_valid = true;
  orientation.scores = {
      SpectralReferenceOrientationScore{"direct", direct_pairing, 0.958},
      SpectralReferenceOrientationScore{"column_flip", column_pairing, 0.248},
  };
  std::ostringstream orientation_json;
  write_patch_report_json(
      orientation_json, "dataset:fixture/raw.RAF", "manual:sg-corners",
      "colorchecker_sg_corner_seeded_projective_grid", meta, 2, 2, "",
      std::nullopt, std::nullopt, "none", {report_patch}, {"A1"},
      std::nullopt, "", geometry, orientation);
  const std::string orientation_doc = orientation_json.str();
  check(orientation_doc.find("\"orientation_validation\"") !=
            std::string::npos,
        "orientation report: block emitted");
  check(orientation_doc.find("\"best_orientation\":\"direct\"") !=
            std::string::npos,
        "orientation report: best orientation emitted");
  check(orientation_doc.find("\"orientation_valid\":true") !=
            std::string::npos,
        "orientation report: valid flag emitted");
  check(orientation_doc.find("\"orientation\":\"column_flip\"") !=
            std::string::npos,
        "orientation report: control orientation emitted");
  check(orientation_doc.find("\"aggregate_score_min_correlation\":0.958") !=
            std::string::npos,
        "orientation report: aggregate score emitted");

  localization = validate_patch_localization_against_oracle(
      shifted, oracle, localization_thresholds);
  localization.oracle_label = "dataset:fixture/rawdigger.csv";
  localization.corner_source =
      "RawDigger-derived least-squares homography validation seed";
  std::ostringstream localization_json;
  write_patch_report_json(
      localization_json, "dataset:fixture/raw.RAF", "manual:sg-corners",
      "colorchecker_sg_corner_seeded_projective_grid", meta, 2, 2, "",
      std::nullopt, std::nullopt, "none", {report_patch}, {"A1"},
      std::nullopt, "", geometry, orientation, localization);
  const std::string localization_doc = localization_json.str();
  check(localization_doc.find("\"localization_validation\"") !=
            std::string::npos,
        "localization report: block emitted");
  check(localization_doc.find("\"max_center_error_px\":6") !=
            std::string::npos,
        "localization report: center gate diagnostic emitted");
  check(localization_doc.find("\"max_abs_mean_error_dn\":25") !=
            std::string::npos,
        "localization report: predeclared DN gate emitted");
  // Pin the machine-readable verdict for a FAILING run. The `passes` boolean is
  // the downstream contract an audit/thesis script reads to decide whether the
  // grid replaced RawDigger; a serialization regression flipping it to true on a
  // real center-gate failure would ship a false claim that no numeric-field
  // check above would catch. The shifted grid must serialize the failing center
  // gate while still reporting correlation passing.
  check(localization_doc.find("\"passes\":false") != std::string::npos,
        "localization report: overall failure verdict serialized");
  check(localization_doc.find("\"center_gate_passes\":false") !=
            std::string::npos,
        "localization report: failing center gate serialized");
  check(localization_doc.find("\"correlation_gate_passes\":true") !=
            std::string::npos,
        "localization report: correlation still passes in failing run");
  check(localization_doc.find("\"center_residuals\"") != std::string::npos,
        "localization report: residual block emitted");
  check(localization_doc.find("\"reference_patch_id\":\"A1\"") !=
            std::string::npos,
        "localization report: residual reference id emitted");
  check(localization_doc.find("\"dx_px\":6") != std::string::npos,
        "localization report: residual dx emitted");

  // Serialized row/column were previously unpinned. Serialize the boundary
  // fixture with geometry/orientation omitted so the only row/column keys come
  // from center_residuals: "row":1 (index 14) and "column":13 (index 13) each
  // occur exactly once, proving both fields serialize and cross the row
  // boundary as nonzero values.
  std::ostringstream wide_json;
  write_patch_report_json(
      wide_json, "dataset:fixture/raw.RAF", "manual:sg-corners",
      "colorchecker_sg_corner_seeded_projective_grid", meta, 2, 2, "",
      std::nullopt, std::nullopt, "none", {report_patch}, {"A1"},
      std::nullopt, "", std::nullopt, std::nullopt, wide);
  const std::string wide_doc = wide_json.str();
  check(wide_doc.find("\"row\":1") != std::string::npos,
        "localization report: residual row serialized across boundary");
  check(wide_doc.find("\"column\":13") != std::string::npos,
        "localization report: residual column serialized across boundary");

  const auto wb_corrected =
      apply_white_balance(flat_corrected, WhiteBalanceGains{0.5, 1.0, 2.0});
  check_near(wb_corrected[0].r, 100.0, 1e-12,
             "wb: red gain applied");
  check_near(wb_corrected[0].g, flat_corrected[0].g, 1e-12,
             "wb: green unity gain");
  check_near(wb_corrected[0].b, flat_corrected[0].b * 2.0, 1e-12,
             "wb: blue gain applied");

  std::ostringstream rgb_csv;
  write_camera_rgb_csv(rgb_csv, patches);
  check(rgb_csv.str().find("5.5,105.5,205.5\n") == 0,
        "rgb csv: writes ccm-fit compatible rows");

  {
    std::ofstream os(root / "rawdigger.csv", std::ios::binary);
    os << "\"Filename\",\"Id\",\"Sample_Name\",\"Camera_Vendor\",\"Camera_Model\","
          "\"ISO\",\"Shutter_String\",\"Aperture_String\",\"Left\",\"Top\","
          "\"Width\",\"Height\",\"Ravg\",\"Gavg\",\"Bavg\"\n"
       << "\"target.RAF\",1,\"A1\",Fujifilm,X-T100,200,1/10,8,10,20,30,40,1.5,2.5,3.5\n"
       << "\"other.RAF\",1,\"A1\",Fujifilm,X-T100,200,1/10,8,99,99,30,40,9,9,9\n"
       << "\"target.RAF\",2,\"A2\",Fujifilm,X-T100,200,1/10,8,50,60,31,41,4.5,5.5,6.5\n";
  }
  const auto rawdigger =
      read_rawdigger_patch_table(root / "rawdigger.csv", "target.RAF");
  check(rawdigger.coords.size() == 2, "rawdigger: filters by filename");
  check_near(rawdigger.coords[0].x, 11.0, 1e-12,
             "rawdigger: left converted to one-based x");
  check_near(rawdigger.coords[0].y, 21.0, 1e-12,
             "rawdigger: top converted to one-based y");
  check_near(rawdigger.reference_rgb[1].b, 6.5, 1e-12,
             "rawdigger: B average parsed");
  check(rawdigger.sample_names[1] == "A2", "rawdigger: sample name parsed");

  {
    std::ofstream os(root / "bad.csv", std::ios::binary);
    os << "1,1,0,2\n";
  }
  bool threw = false;
  try {
    (void)read_patch_coords_csv(root / "bad.csv");
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "coords: non-positive width rejected");

  std::filesystem::remove_all(root);
}
