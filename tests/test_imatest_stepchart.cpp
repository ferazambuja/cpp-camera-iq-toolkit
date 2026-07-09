#include "camera_iq/imatest_stepchart.hpp"

#include <cstdio>
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

struct FixtureOpts {
  int zones = 20;
  bool pixel_reversal = false;
  bool flat_pixel = false;
  bool non_decreasing_log = false;
  std::string version = "4.5.7";
  bool omit_run_date = false;
  bool omit_file_name = false;
  std::string file_count_line = "10 files combined for analysis.";
};

// Mirrors the real archive layout: Title line, blank section separators,
// space-padded numeric cells, 10-cell header vs 8-field rows, full 20-row
// Y-density poison table, Exif decoy block.
std::string stepchart_fixture(const FixtureOpts& opts = {}) {
  std::string text;
  text += "Imatest," + opts.version + ", , Stepchart\n";
  text += "Title,10 file avg (NIKON D800_i100_s1-40_2.NEF & 9 more)\n";
  if (!opts.omit_run_date) {
    text += "Run date,11-Dec-2016 03:19:31,,Build 2016-11-22\n";
  }
  if (!opts.omit_file_name) {
    text += "File name,NIKON D800_i100_s1-40_2.NEF\n";
  }
  text += "\n";
  text += opts.file_count_line + "\n";
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
  text += "\n";
  text += "Zones," + std::to_string(opts.zones) + "\n";
  text += "\n";
  text += "Pixel offset,0\n";
  text += "\n";
  text +=
      "Log exposure and analysis levels (Luminance (Y) channel for color "
      "images)\n";
  text +=
      "Zone,Pixel,Pixel/255,Log(exp),Log(px/255),Width px,Height px,"
      "Pixels total,Lux (patch),\n";
  for (int z = 1; z <= opts.zones; ++z) {
    double pixel =
        opts.flat_pixel ? 1.0 : 42.4 - static_cast<double>(z - 1) * 2.2;
    if (z >= 19 && !opts.flat_pixel) pixel = 0.4;  // real deep-shadow tie.
    if (opts.pixel_reversal && z == 10) pixel = 30.0;
    double log_exp = -0.15 * static_cast<double>(z - 1);
    if (opts.non_decreasing_log && z == 5) log_exp = -0.2;
    const int width = z == 20 ? 200 : 201;
    const int pixels = z == 20 ? 10000 : 10050;
    char row[160];
    if (z == 1) {
      std::snprintf(row, sizeof(row),
                    "%2d, %6.1f, %6.4f, -0.0000, -1.0000,%d,50,%d\n", z, pixel,
                    pixel / 255.0, width, pixels);
    } else {
      std::snprintf(row, sizeof(row),
                    "%2d, %6.1f, %6.4f, %7.4f, -1.0000,%d,50,%d\n", z, pixel,
                    pixel / 255.0, log_exp, width, pixels);
    }
    text += row;
  }
  text += "\n";
  text += "Zone,(-)Log(exp),Y-density\n";
  for (int z = 1; z <= 20; ++z) {
    char row[64];
    std::snprintf(row, sizeof(row), "%2d, %6.4f, %6.4f,0,0,0\n", z, 0.15 * z,
                  0.1 * z);
    text += row;
  }
  text += "\n";
  text += "1,Low,6.19\n";
  text += "0.5,Medium,4.77\n";
  text += "Frequency (C/P),Noise power\n";
  text += "  0.005,  0.069\n";
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

// Replaces the first primary-table row starting with the padded zone prefix
// (e.g. "\n 2,") with `replacement` (no trailing newline).
std::string replace_zone_row(std::string text, const std::string& prefix,
                             const std::string& replacement) {
  const auto pos = text.find(prefix);
  const auto end = text.find('\n', pos + 1);
  text.replace(pos + 1, end - pos - 1, replacement);
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

  {
    FixtureOpts other_version;
    other_version.version = "4.6.1";
    const fs::path p = root / "other_version.csv";
    write_file(p, stepchart_fixture(other_version));
    const auto parsed = read_imatest_stepchart_summary(p);
    check(parsed.imatest_version == "4.6.1",
          "stepchart parser: other Imatest versions accepted, version is data");
  }

  write_file(root / "bad_non_stepchart.csv", "not,imatest\n");
  check(throws_containing(root / "bad_non_stepchart.csv", "Stepchart"),
        "stepchart parser: rejects non-Stepchart CSV");

  {
    FixtureOpts n1;
    n1.zones = 1;
    write_file(root / "bad_n1.csv", stepchart_fixture(n1));
    check(throws_containing(root / "bad_n1.csv", "N < 2"),
          "stepchart parser: rejects N < 2 before monotonic gates");
  }

  {
    FixtureOpts reversal;
    reversal.pixel_reversal = true;
    write_file(root / "bad_reversal.csv", stepchart_fixture(reversal));
    check(throws_containing(root / "bad_reversal.csv", "pixel monotonic"),
          "stepchart parser: rejects pixel reversal");
  }

  {
    FixtureOpts flat;
    flat.flat_pixel = true;
    write_file(root / "bad_flat.csv", stepchart_fixture(flat));
    check(throws_containing(root / "bad_flat.csv", "endpoint"),
          "stepchart parser: rejects all-flat pixel column");
  }

  {
    FixtureOpts bad_log;
    bad_log.non_decreasing_log = true;
    write_file(root / "bad_log.csv", stepchart_fixture(bad_log));
    check(throws_containing(root / "bad_log.csv", "Log(exp)"),
          "stepchart parser: rejects non-decreasing Log(exp)");
  }

  {
    std::string bad_count = stepchart_fixture();
    const auto pos = bad_count.find("File 10,");
    bad_count.erase(pos, bad_count.find('\n', pos) - pos + 1);
    write_file(root / "bad_count.csv", bad_count);
    check(throws_containing(root / "bad_count.csv", "file-list count"),
          "stepchart parser: rejects file-list count mismatch");
  }

  {
    // Duplicated 'File 3' with 'File 4' removed keeps the count at M but
    // breaks the 1..M index sequence.
    std::string dup_file = stepchart_fixture();
    const auto pos = dup_file.find("File 4,");
    dup_file.replace(pos, 6, "File 3");
    write_file(root / "bad_dup_file_row.csv", dup_file);
    check(throws_containing(root / "bad_dup_file_row.csv",
                            "file-list rows"),
          "stepchart parser: rejects duplicate/non-contiguous file-list rows");
  }

  {
    std::string escaped = stepchart_fixture();
    const auto pos = escaped.find("File 1,");
    const auto end = escaped.find('\n', pos);
    escaped.replace(pos, end - pos, "File 1, ../outside.NEF");
    write_file(root / "bad_escaped_file_row.csv", escaped);
    check(throws_containing(root / "bad_escaped_file_row.csv",
                            "file-list filename"),
          "stepchart parser: rejects parent-path file-list entries");
  }

  {
    std::string nested = stepchart_fixture();
    const auto pos = nested.find("File 1,");
    const auto end = nested.find('\n', pos);
    nested.replace(pos, end - pos, "File 1, nested/NIKON D800_i100_s1-40_2.NEF");
    write_file(root / "bad_nested_file_row.csv", nested);
    check(throws_containing(root / "bad_nested_file_row.csv",
                            "file-list filename"),
          "stepchart parser: rejects nested-path file-list entries");
  }

  {
    std::string duplicate_empty_header = stepchart_fixture();
    const auto pos = duplicate_empty_header.find(
        "Zone,Pixel,Pixel/255,Log(exp),Log(px/255),Width px,Height px,");
    duplicate_empty_header.insert(
        pos,
        "Zone,Pixel,Pixel/255,Log(exp),Log(px/255),Width px,Height px,"
        "Pixels total,Lux (patch),\n\n");
    write_file(root / "bad_duplicate_empty_header.csv", duplicate_empty_header);
    check(throws_containing(root / "bad_duplicate_empty_header.csv",
                            "duplicate primary table"),
          "stepchart parser: rejects duplicate primary header even before rows");
  }

  {
    // Zone 5 renumbered to 6: duplicate-6/missing-5 in one mutation.
    std::string renumbered =
        replace_zone_row(stepchart_fixture(), "\n 5,",
                         " 6,   33.6, 0.1318, -0.6000, -1.0000,201,50,10050");
    write_file(root / "bad_renumbered_zone.csv", renumbered);
    check(throws_containing(root / "bad_renumbered_zone.csv",
                            "missing or non-contiguous zone rows"),
          "stepchart parser: rejects duplicate/missing/non-contiguous zones");
  }

  {
    // A 21st zone row directly after zone 20 (before the table's blank-line
    // boundary) must be rejected, not silently dropped.
    std::string extra = stepchart_fixture();
    const auto pos = extra.find("\n20,");
    const auto end = extra.find('\n', pos + 1);
    const std::string zone20_line = extra.substr(pos, end - pos);
    extra.insert(end, zone20_line);
    write_file(root / "bad_extra_row.csv", extra);
    check(throws_containing(root / "bad_extra_row.csv",
                            "more rows than Zones,N"),
          "stepchart parser: rejects extra primary rows beyond Zones,N");
  }

  {
    // Deleting the final zone row hits the end-of-parse row-count gate.
    std::string short_rows = stepchart_fixture();
    const auto pos = short_rows.find("\n20,");
    short_rows.erase(pos, short_rows.find('\n', pos + 1) - pos);
    write_file(root / "bad_short_rows.csv", short_rows);
    check(throws_containing(root / "bad_short_rows.csv",
                            "primary row count mismatch"),
          "stepchart parser: rejects primary row count below Zones,N");
  }

  {
    std::string bad_pixel255 =
        replace_zone_row(stepchart_fixture(), "\n 2,",
                         " 2,   40.2, -0.1000, -0.1500, -1.0000,201,50,10050");
    write_file(root / "bad_pixel255.csv", bad_pixel255);
    check(throws_containing(root / "bad_pixel255.csv", "Pixel/255"),
          "stepchart parser: rejects negative Pixel/255");
  }

  {
    std::string bad_pixel =
        replace_zone_row(stepchart_fixture(), "\n20,",
                         "20,   -1.0, 0.0000, -2.8500, -1.0000,200,50,10000");
    write_file(root / "bad_negative_pixel.csv", bad_pixel);
    check(throws_containing(root / "bad_negative_pixel.csv", "negative Pixel"),
          "stepchart parser: rejects negative Pixel values");
  }

  {
    std::string inf_pixel255 =
        replace_zone_row(stepchart_fixture(), "\n 2,",
                         " 2,   40.2, Inf, -0.1500, -1.0000,201,50,10050");
    write_file(root / "bad_inf_pixel255.csv", inf_pixel255);
    check(throws_containing(root / "bad_inf_pixel255.csv", "Pixel/255"),
          "stepchart parser: rejects non-finite Pixel/255");
  }

  {
    std::string zero_width =
        replace_zone_row(stepchart_fixture(), "\n 2,",
                         " 2,   40.2, 0.1576, -0.1500, -1.0000,0,50,10050");
    write_file(root / "bad_zero_width.csv", zero_width);
    check(throws_containing(root / "bad_zero_width.csv", "geometry"),
          "stepchart parser: rejects zero width");
  }

  {
    // Out-of-int-range Pixels total must be rejected, not UB-cast.
    std::string overflow = replace_zone_row(
        stepchart_fixture(), "\n 2,",
        " 2,   40.2, 0.1576, -0.1500, -1.0000,201,50,4294967296");
    write_file(root / "bad_overflow.csv", overflow);
    check(throws_containing(root / "bad_overflow.csv", "Pixels total"),
          "stepchart parser: rejects out-of-int-range Pixels total");
  }

  {
    FixtureOpts no_date;
    no_date.omit_run_date = true;
    write_file(root / "bad_no_date.csv", stepchart_fixture(no_date));
    check(throws_containing(root / "bad_no_date.csv", "missing run date"),
          "stepchart parser: rejects missing run date");
  }

  {
    FixtureOpts no_name;
    no_name.omit_file_name = true;
    write_file(root / "bad_no_name.csv", stepchart_fixture(no_name));
    check(throws_containing(root / "bad_no_name.csv", "missing file name"),
          "stepchart parser: rejects missing file name");
  }

  {
    // An absurd file count must stay inside the runtime_error taxonomy, not
    // escape as std::out_of_range from std::stoi.
    FixtureOpts absurd;
    absurd.file_count_line = "99999999999 files combined for analysis.";
    write_file(root / "bad_absurd_count.csv", stepchart_fixture(absurd));
    check(throws_containing(root / "bad_absurd_count.csv", "file-count"),
          "stepchart parser: absurd file count rejected as runtime_error");
  }

  fs::remove_all(root);
}
