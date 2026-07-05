#include "camera_iq/patches.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#include "camera_iq/demosaic.hpp"
#include "harness.hpp"

using camera_iq::CameraRgbPatch;
using camera_iq::PatchChannelComparison;
using camera_iq::PatchCoord;
using camera_iq::compare_patch_means_to_rgb;
using camera_iq::extract_patch_means;
using camera_iq::read_patch_coords_csv;
using camera_iq::read_rawdigger_patch_table;
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
  check_near(comparison.channels[1].correlation, -1.0, 1e-12,
             "compare: green negative correlation");
  check_near(comparison.channels[1].rmse_after_affine, 0.0, 1e-12,
             "compare: affine rmse");

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
