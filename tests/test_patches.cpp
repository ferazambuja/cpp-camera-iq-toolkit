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
using camera_iq::PatchChannelComparison;
using camera_iq::PatchCoord;
using camera_iq::PatchMean;
using camera_iq::RawMeta;
using camera_iq::WhiteBalanceGains;
using camera_iq::apply_flat_field;
using camera_iq::apply_white_balance;
using camera_iq::compare_patch_means_to_rgb;
using camera_iq::extract_patch_means;
using camera_iq::flat_field_near_ceiling_threshold_fraction;
using camera_iq::flat_field_normalization_policy;
using camera_iq::read_patch_coords_csv;
using camera_iq::read_rawdigger_patch_table;
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
