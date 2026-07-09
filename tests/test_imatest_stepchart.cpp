#include "camera_iq/imatest_stepchart.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::read_imatest_stepchart_summary;
using test::check;
using test::check_near;

namespace {

void write_file(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream os(path, std::ios::binary);
  os << text;
}

std::string stepchart_fixture(int zones = 20, bool pixel_reversal = false,
                              bool flat_pixel = false,
                              bool non_decreasing_log = false) {
  std::string text;
  text += "Imatest,4.5.7, , Stepchart\n";
  text += "Run date,11-Dec-2016 03:19:31,,Build 2016-11-22\n";
  text += "File name,NIKON D800_i100_s1-40_2.NEF\n";
  text += "10 files combined for analysis.\n";
  text += "File 1, NIKON D800_i100_s1-40_2.NEF\n";
  text += "File 2, NIKON D800_i100_s1-40_3.NEF\n";
  text += "File 3, NIKON D800_i100_s1-40_4.NEF\n";
  text += "File 4, NIKON D800_i100_s1-40_5.NEF\n";
  text += "File 5, NIKON D800_i100_s1-40_6.NEF\n";
  text += "File 6, NIKON D800_i100_s1-40_7.NEF\n";
  text += "File 7, NIKON D800_i100_s1-40_8.NEF\n";
  text += "File 8, NIKON D800_i100_s1-40_9.NEF\n";
  text += "File 9, NIKON D800_i100_s1-40_91.NEF\n";
  text += "File 10, NIKON D800_i100_s1-40_1.NEF\n";
  text += "Zones," + std::to_string(zones) + "\n";
  text += "Pixel offset,0\n";
  text += "Some prose caption before the primary table\n";
  text +=
      "Zone,Pixel,Pixel/255,Log(exp),Log(px/255),Width px,Height px,"
      "Pixels total,Lux (patch),\n";
  for (int z = 1; z <= zones; ++z) {
    double pixel = flat_pixel ? 1.0 : 42.4 - static_cast<double>(z - 1) * 2.2;
    if (z >= 19 && !flat_pixel) pixel = 0.4;  // real deep-shadow tie.
    if (pixel_reversal && z == 10) pixel = 30.0;
    double log_exp = z == 1 ? -0.0 : -0.15 * static_cast<double>(z - 1);
    if (non_decreasing_log && z == 5) log_exp = -0.2;
    const int width = z == 20 ? 200 : 201;
    const int pixels = z == 20 ? 10000 : 10050;
    text += (z < 10 ? " " : "") + std::to_string(z) + "," +
            std::to_string(pixel) + "," + std::to_string(pixel / 255.0) +
            "," + (z == 1 ? "-0.0000" : std::to_string(log_exp)) +
            ",-1.0000," + std::to_string(width) + ",50," +
            std::to_string(pixels) + "\n";
  }
  text += "\n";
  text += "Zone,(-)Log(exp),Y-density\n";
  text += "1,0.0000,0.0000,0,0,0\n";
  text += "20,3.0000,2.0000,0,0,0\n";
  text += "\n";
  text += "1,Low,6.19\n";
  text += "0.5,Medium,4.77\n";
  text += "Frequency (C/P),Noise power\n";
  text += "1,9999\n";
  text += "Zone,Mean noise,S/N\n";
  text += "1,,Inf\n";
  text += "20,,Inf\n";
  text += "Directory                       , /""Users/private/leak/path\n";
  text += "Directory Number                , 000\n";
  text += "File Name                       , NIKON D800_i100_s1-40_2.NEF\n";
  text += "File Size                       , 36 MB\n";
  text += "File Source                     , Digital Camera\n";
  return text;
}

bool throws_containing(const fs::path& path, const std::string& needle) {
  try {
    (void)read_imatest_stepchart_summary(path);
  } catch (const std::runtime_error& e) {
    return std::string(e.what()).find(needle) != std::string::npos;
  }
  return false;
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_stepchart";
  fs::remove_all(root);
  fs::create_directories(root);

  const fs::path good = root / "good_summary.csv";
  write_file(good, stepchart_fixture());
  const auto summary = read_imatest_stepchart_summary(good);

  check(summary.imatest_version == "4.5.7",
        "stepchart parser: Imatest version parsed");
  check(summary.run_date == "11-Dec-2016 03:19:31",
        "stepchart parser: run date parsed from cell 2 only");
  check(summary.file_name == "NIKON D800_i100_s1-40_2.NEF",
        "stepchart parser: file name parsed");
  check(summary.declared_file_count == 10,
        "stepchart parser: prose M-line parsed");
  check(summary.combined_files.size() == 10,
        "stepchart parser: file-list count parsed");
  check(summary.combined_files[8] == "NIKON D800_i100_s1-40_91.NEF",
        "stepchart parser: preserves non-contiguous _91 file row");
  check(summary.declared_zone_count == 20,
        "stepchart parser: declared zone count parsed");
  check(summary.zones.size() == 20,
        "stepchart parser: 20 primary rows parsed");
  check(summary.zones.front().zone == 1 && summary.zones.back().zone == 20,
        "stepchart parser: first and last zone IDs parsed");
  check_near(summary.zones.front().log_exposure, 0.0, 0.0,
             "stepchart parser: negative zero log exposure parses");
  check(summary.zones[18].pixel == summary.zones[19].pixel,
        "stepchart parser: tied shadow pixels are accepted");
  check(summary.zones.back().width_px == 200 &&
            summary.zones.front().width_px == 201,
        "stepchart parser: zone-20 narrower geometry preserved");

  write_file(root / "bad_non_stepchart.csv", "not,imatest\n");
  check(throws_containing(root / "bad_non_stepchart.csv", "Stepchart"),
        "stepchart parser: rejects non-Stepchart CSV");

  write_file(root / "bad_n1.csv", stepchart_fixture(1));
  check(throws_containing(root / "bad_n1.csv", "N < 2"),
        "stepchart parser: rejects N < 2 before monotonic gates");

  write_file(root / "bad_reversal.csv",
             stepchart_fixture(20, true, false, false));
  check(throws_containing(root / "bad_reversal.csv", "pixel monotonic"),
        "stepchart parser: rejects pixel reversal");

  write_file(root / "bad_flat.csv", stepchart_fixture(20, false, true, false));
  check(throws_containing(root / "bad_flat.csv", "endpoint"),
        "stepchart parser: rejects all-flat pixel column");

  write_file(root / "bad_log.csv", stepchart_fixture(20, false, false, true));
  check(throws_containing(root / "bad_log.csv", "Log(exp)"),
        "stepchart parser: rejects non-decreasing Log(exp)");

  std::string bad_count = stepchart_fixture();
  const auto pos = bad_count.find("File 10,");
  bad_count.erase(pos, bad_count.find('\n', pos) - pos + 1);
  write_file(root / "bad_count.csv", bad_count);
  check(throws_containing(root / "bad_count.csv", "file-list count"),
        "stepchart parser: rejects file-list count mismatch");

  std::string bad_pixel255 = stepchart_fixture();
  const auto row_pos = bad_pixel255.find("\n 2,") + 1;
  const auto next = bad_pixel255.find('\n', row_pos);
  bad_pixel255.replace(row_pos, next - row_pos,
                       " 2,40,-0.1,-0.1500,-1.0000,201,50,10050");
  write_file(root / "bad_pixel255.csv", bad_pixel255);
  check(throws_containing(root / "bad_pixel255.csv", "Pixel/255"),
        "stepchart parser: rejects negative Pixel/255");

  fs::remove_all(root);
}
