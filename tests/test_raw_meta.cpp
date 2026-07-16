#include "camera_iq/raw_meta.hpp"

#include <filesystem>
#include <fstream>

#include "harness.hpp"

namespace fs = std::filesystem;
using camera_iq::cfa_pattern_string;
using camera_iq::effective_black_levels;
using camera_iq::effective_raw_stride_pixels;
using camera_iq::is_supported_bayer_filter;
using camera_iq::read_raw_cfa_image;
using camera_iq::read_raw_metadata;
using test::check;

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

  // LibRaw uses filters >= 1000 for ordinary Bayer masks. Special values below
  // 1000 include 0 (full color/monochrome), 1 (16x16 Leaf), and 9 (Fuji X-Trans),
  // none of which the 2x2 CFA stats path supports.
  check(is_supported_bayer_filter(0x94949494u), "filters: Bayer mask accepted");
  check(!is_supported_bayer_filter(0u), "filters: full-color/monochrome rejected");
  check(!is_supported_bayer_filter(9u), "filters: X-Trans rejected");
  check(!is_supported_bayer_filter(1u), "filters: non-2x2 special mask rejected");

  check(effective_raw_stride_pixels(0, 6016) == 6016,
        "stride: missing raw_pitch falls back to raw_width");
  check(effective_raw_stride_pixels(12032, 6016) == 6016,
        "stride: byte pitch converted to uint16 pixels");
  check(effective_raw_stride_pixels(12033, 6016) == 0,
        "stride: odd byte pitch rejected");

  // Effective black — the Fuji X-T100 case: scalar black and cblack[0..3] are 0,
  // the real ~1024 DN pedestal lives in the 2x2 cblack[6..] tile. Reading the
  // scalar alone would report 0 (the bug this exercises).
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
    cb[0] = 10; cb[1] = 20; cb[2] = 30; cb[3] = 40;
    const auto b = effective_black_levels(100, cb, 16, {0, 1, 2, 3});
    check(b[0] == 110 && b[1] == 120 && b[2] == 130 && b[3] == 140,
          "black: scalar + per-channel, no tile");
  }
  // Non-uniform 2x2 tile mapped through COLOR() indices.
  {
    unsigned cb[16] = {0};
    cb[4] = 2; cb[5] = 2;
    cb[6] = 500; cb[7] = 501; cb[8] = 510; cb[9] = 511;  // per-position
    const auto b = effective_black_levels(0, cb, 16, {0, 1, 2, 3});
    check(b[0] == 500 && b[1] == 501 && b[2] == 510 && b[3] == 511,
          "black: non-uniform tile per position");
  }
  // The cblack tile phase is anchored to the active-area origin, matching
  // LibRaw's subtract_black/raw2image convention and DNG BlackLevel semantics.
  // Cropping margins must not shift the per-position tile values.
  {
    unsigned cb[16] = {0};
    cb[4] = 2; cb[5] = 2;
    cb[6] = 500; cb[7] = 501; cb[8] = 510; cb[9] = 511;
    const auto b = effective_black_levels(0, cb, 16, {0, 1, 2, 3});
    check(b[0] == 500 && b[1] == 501 && b[2] == 510 && b[3] == 511,
          "black: tile phase remains active-area-local");
  }
  // Out-of-range tile dimensions must not read past the buffer.
  {
    unsigned cb[16] = {0};
    cb[4] = 2; cb[5] = 1000;  // (1,*) tile index would be ~1006 >> 16
    const auto b = effective_black_levels(5, cb, 16, {0, 1, 2, 3});
    check(b[0] == 5 && b[1] == 5 && b[2] == 5 && b[3] == 5,
          "black: out-of-range tile ignored (no OOB read)");
  }

  check(!read_raw_metadata("/nonexistent/file.RAF").has_value(),
        "missing file yields nullopt");
  check(!read_raw_cfa_image("/nonexistent/file.RAF").has_value(),
        "missing file yields no CFA image");

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
