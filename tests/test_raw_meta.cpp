#include <libraw/libraw.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "camera_iq/commands.hpp"
#include "camera_iq/raw_meta.hpp"
#include "harness.hpp"

namespace fs = std::filesystem;
using camera_iq::black_repeat_is_cfa_periodic;
using camera_iq::body_serial_string;
using camera_iq::cfa_pattern_string;
using camera_iq::cmd_raw_stats;
using camera_iq::effective_black_levels;
using camera_iq::effective_raw_stride_pixels;
using camera_iq::is_supported_bayer_filter;
using camera_iq::measurement_raw_meta_from_processor;
using camera_iq::raw_meta_from_processor;
using camera_iq::read_raw_cfa_image;
using camera_iq::read_raw_metadata;
using camera_iq::write_raw_stats_json;
using test::check;

namespace {

int run_raw_stats(const std::vector<std::string>& args) {
  std::vector<std::string> storage = args;
  std::vector<char*> argv;
  argv.reserve(storage.size());
  for (auto& arg : storage) argv.push_back(arg.data());
  return cmd_raw_stats(static_cast<int>(argv.size()), argv.data());
}

std::string read_text(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

void write_dataset_config(const fs::path& path, const fs::path& dataset_root) {
  std::ofstream output(path, std::ios::binary);
  output << "{\"datasets\":{\"fixture\":{\"root\":\""
         << dataset_root.generic_string() << "\"}}}";
}

}  // namespace

void TESTS() {
  // LibRaw Bayer cdesc is "RGBG": index 0=R, 1=G, 2=B, 3=second G.
  check(cfa_pattern_string("RGBG", {0, 1, 3, 2}) == "RGGB",
        "cfa: RGGB from RGBG descriptor");
  check(cfa_pattern_string("RGBG", {1, 0, 2, 3}) == "GRBG", "cfa: GRBG");
  check(cfa_pattern_string("RGBG", {2, 3, 1, 0}) == "BGGR", "cfa: BGGR");
  check(cfa_pattern_string("RGBG", {0, 1, 3, 7}).empty(),
        "cfa: out-of-range index rejected");
  check(cfa_pattern_string("", {0, 0, 0, 0}).empty(),
        "cfa: empty descriptor rejected");

  const char serial_with_nul[8] = {'X', 'T', '1', '0', '0', '\0', 'x', 'x'};
  check(body_serial_string(serial_with_nul, sizeof(serial_with_nul)) == "XT100",
        "body serial: fixed LibRaw buffer stops at NUL");
  const char serial_without_nul[4] = {'A', 'B', 'C', 'D'};
  check(body_serial_string(serial_without_nul, sizeof(serial_without_nul)) ==
            "ABCD",
        "body serial: fixed LibRaw buffer is capacity bounded");
  check(body_serial_string(nullptr, 64).empty(),
        "body serial: null metadata buffer is empty");

  {
    LibRaw processor;
    std::snprintf(processor.imgdata.idata.make,
                  sizeof(processor.imgdata.idata.make), "%s", "TEST MAKE");
    std::snprintf(processor.imgdata.idata.model,
                  sizeof(processor.imgdata.idata.model), "%s", "TEST MODEL");
    std::snprintf(processor.imgdata.shootinginfo.BodySerial,
                  sizeof(processor.imgdata.shootinginfo.BodySerial), "%s",
                  "BODY-42");
    const auto meta = raw_meta_from_processor(processor);
    check(meta.make == "TEST MAKE" && meta.model == "TEST MODEL" &&
              meta.body_serial == "BODY-42",
          "body serial: LibRaw shooting-info field maps into RawMeta");
  }

  // LibRaw uses filters >= 1000 for ordinary Bayer masks. Special values below
  // 1000 include 0 (full color/monochrome), 1 (16x16 Leaf), and 9 (Fuji
  // X-Trans), none of which the 2x2 CFA stats path supports.
  check(is_supported_bayer_filter(0x94949494u), "filters: Bayer mask accepted");
  check(!is_supported_bayer_filter(0u),
        "filters: full-color/monochrome rejected");
  check(!is_supported_bayer_filter(9u), "filters: X-Trans rejected");
  check(!is_supported_bayer_filter(1u),
        "filters: non-2x2 special mask rejected");

  check(effective_raw_stride_pixels(0, 6016) == 6016,
        "stride: missing raw_pitch falls back to raw_width");
  check(effective_raw_stride_pixels(12032, 6016) == 6016,
        "stride: byte pitch converted to uint16 pixels");
  check(effective_raw_stride_pixels(12033, 6016) == 0,
        "stride: odd byte pitch rejected");

  {
    camera_iq::RawCfaReport report;
    report.near_ceiling_level = 0.90;
    report.planes[0].label = "R";
    report.planes[0].count = 4;
    report.planes[0].near_ceiling_fraction = 0.25;
    std::ostringstream json;
    write_raw_stats_json(json, "fixture.RAF", report);
    check(json.str().find("\"near_ceiling_level\":0.9") != std::string::npos,
          "raw-stats JSON: effective near-ceiling policy is serialized");
    check(
        json.str().find("\"near_ceiling_fraction\":0.25") != std::string::npos,
        "raw-stats JSON: derived near-ceiling fraction is serialized");
  }

  // Effective black — the Fuji X-T100 case: scalar black and cblack[0..3] are
  // 0, the real ~1024 DN pedestal lives in the 2x2 cblack[6..] tile. Reading
  // the scalar alone would report 0 (the bug this exercises).
  {
    unsigned cb[16] = {0};
    cb[4] = 2;  // tile rows
    cb[5] = 2;  // tile cols
    cb[6] = cb[7] = cb[8] = cb[9] = 1024;
    const auto b = effective_black_levels(0, cb, 16, {0, 1, 3, 2});
    check(b[0] == 1024 && b[1] == 1024 && b[2] == 1024 && b[3] == 1024,
          "black: tile pedestal recovered (1024, not 0)");
  }
  // Scalar + per-channel offsets, no tile (bh=bw=0).
  {
    unsigned cb[16] = {0};
    cb[0] = 10;
    cb[1] = 20;
    cb[2] = 30;
    cb[3] = 40;
    const auto b = effective_black_levels(100, cb, 16, {0, 1, 2, 3});
    check(b[0] == 110 && b[1] == 120 && b[2] == 130 && b[3] == 140,
          "black: scalar + per-channel, no tile");
  }
  // Non-uniform 2x2 tile mapped through COLOR() indices.
  {
    unsigned cb[16] = {0};
    cb[4] = 2;
    cb[5] = 2;
    cb[6] = 500;
    cb[7] = 501;
    cb[8] = 510;
    cb[9] = 511;  // per-position
    const auto b = effective_black_levels(0, cb, 16, {0, 1, 2, 3});
    check(b[0] == 500 && b[1] == 501 && b[2] == 510 && b[3] == 511,
          "black: non-uniform tile per position");
  }
  // Exercise the LibRaw bridge rather than restating effective_black_levels().
  // Odd sensor margins must not phase-shift a tile defined in active-image
  // coordinates.
  {
    LibRaw processor;
    processor.imgdata.idata.filters = 0x94949494u;
    processor.imgdata.sizes.top_margin = 3;
    processor.imgdata.sizes.left_margin = 5;
    auto& cb = processor.imgdata.color.cblack;
    cb[4] = 2;
    cb[5] = 2;
    cb[6] = 500;
    cb[7] = 501;
    cb[8] = 510;
    cb[9] = 511;
    const auto meta = raw_meta_from_processor(processor);
    check(meta.black_per_channel[0] == 500 &&
              meta.black_per_channel[1] == 501 &&
              meta.black_per_channel[2] == 510 &&
              meta.black_per_channel[3] == 511,
          "black: odd raw margins do not shift active-area tile phase");
    check(meta.black_repeat_is_cfa_periodic,
          "black: ordinary 2x2 repeat is representable per CFA position");
    const auto measurement_meta =
        measurement_raw_meta_from_processor(processor);
    check(measurement_meta.has_value() &&
              measurement_meta->black_per_channel == meta.black_per_channel,
          "black: post-unpack measurement metadata accepts a 2x2 repeat");
  }
  // DOC-EVIDENCE: raw-foundation.black-repeat-periodicity
  // A larger repeat can be represented by four CFA-position values only when
  // every same-parity entry agrees. Otherwise the RAW path must reject rather
  // than subtract the top-left 2x2 across the entire image.
  {
    check(black_repeat_is_cfa_periodic(nullptr, 0),
          "black: absent repeat metadata is an empty tile");
    unsigned cb[24] = {0};
    cb[4] = 4;
    cb[5] = 4;
    for (int row = 0; row < 4; ++row) {
      for (int col = 0; col < 4; ++col) {
        cb[6 + row * 4 + col] =
            static_cast<unsigned>(500 + (row % 2) * 10 + (col % 2));
      }
    }
    check(black_repeat_is_cfa_periodic(cb, 24),
          "black: larger tile repeating by CFA parity is supported");
    cb[6 + 2 * 4 + 2] = 999;
    check(!black_repeat_is_cfa_periodic(cb, 24),
          "black: same-parity variation in a larger tile is rejected");
    check(!black_repeat_is_cfa_periodic(cb, 20),
          "black: incomplete repeat tile is rejected");

    unsigned odd_cb[9] = {0};
    odd_cb[4] = 3;
    odd_cb[5] = 1;
    odd_cb[6] = 5;
    odd_cb[7] = 6;
    odd_cb[8] = 5;
    check(!black_repeat_is_cfa_periodic(odd_cb, 9),
          "black: odd repeat period is checked over its full CFA phase cycle");
    odd_cb[7] = 5;
    check(black_repeat_is_cfa_periodic(odd_cb, 9),
          "black: constant odd repeat period remains representable");

    LibRaw processor;
    auto& processor_cb = processor.imgdata.color.cblack;
    processor_cb[4] = 4;
    processor_cb[5] = 4;
    for (int index = 0; index < 16; ++index) {
      processor_cb[6 + index] = cb[6 + index];
    }
    check(!raw_meta_from_processor(processor).black_repeat_is_cfa_periodic,
          "black: LibRaw bridge exposes an unsupported spatial repeat");
    check(!measurement_raw_meta_from_processor(processor).has_value(),
          "black: post-unpack measurement metadata refuses spatial repeats");
  }
  // Out-of-range tile dimensions must not read past the buffer.
  {
    unsigned cb[16] = {0};
    cb[4] = 2;
    cb[5] = 1000;  // (1,*) tile index would be ~1006 >> 16
    const auto b = effective_black_levels(5, cb, 16, {0, 1, 2, 3});
    check(b[0] == 5 && b[1] == 5 && b[2] == 5 && b[3] == 5,
          "black: out-of-range tile ignored (no OOB read)");
  }

  check(!read_raw_metadata("/nonexistent/file.RAF").has_value(),
        "missing file yields nullopt");
  check(!read_raw_cfa_image("/nonexistent/file.RAF").has_value(),
        "missing file yields no CFA image");

  const fs::path alias_input =
      fs::temp_directory_path() / "camera_iq_raw_stats_alias.RAF";
  {
    std::ofstream os(alias_input, std::ios::binary);
    os << "source evidence";
  }
  check(
      run_raw_stats({alias_input.string(), "--out", alias_input.string()}) == 2,
      "raw-stats command refuses output that aliases its input");
  check(read_text(alias_input) == "source evidence",
        "raw-stats alias refusal preserves input bytes");
  fs::remove(alias_input);

  const fs::path dataset_fixture =
      fs::temp_directory_path() / "camera_iq_raw_stats_dataset_escape";
  fs::remove_all(dataset_fixture);
  fs::create_directories(dataset_fixture / "dataset" / "Images");
  const fs::path outside_raw = dataset_fixture / "outside.RAF";
  {
    std::ofstream os(outside_raw, std::ios::binary);
    os << "outside evidence";
  }
  const fs::path dataset_config = dataset_fixture / "datasets.local.json";
  write_dataset_config(dataset_config, dataset_fixture / "dataset");
  check(run_raw_stats({"../outside.RAF", "--dataset", "fixture", "--config",
                       dataset_config.string()}) == 2,
        "raw-stats command rejects leading dataset traversal");
  check(run_raw_stats({"Images/../../outside.RAF", "--dataset", "fixture",
                       "--config", dataset_config.string()}) == 2,
        "raw-stats command rejects embedded dataset traversal");
  fs::create_symlink(outside_raw,
                     dataset_fixture / "dataset" / "escaped-link.RAF");
  check(run_raw_stats({"escaped-link.RAF", "--dataset", "fixture", "--config",
                       dataset_config.string()}) == 2,
        "raw-stats command rejects dataset symlink escape");
  check(read_text(outside_raw) == "outside evidence",
        "raw-stats dataset escape refusals preserve outside bytes");
  fs::remove_all(dataset_fixture);

  const fs::path garbage = fs::temp_directory_path() / "camera_iq_garbage.RAF";
  {
    std::ofstream os(garbage, std::ios::binary);
    os << "this is not a raw file";
  }
  check(!read_raw_metadata(garbage).has_value(), "garbage file yields nullopt");
  check(!read_raw_cfa_image(garbage).has_value(),
        "garbage file yields no CFA image");
  fs::remove(garbage);
}
