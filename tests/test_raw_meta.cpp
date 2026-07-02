#include "camera_iq/raw_meta.hpp"

#include <filesystem>
#include <fstream>

#include "harness.hpp"

namespace fs = std::filesystem;
using camera_iq::cfa_pattern_string;
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

  check(!read_raw_metadata("/nonexistent/file.RAF").has_value(),
        "missing file yields nullopt");

  const fs::path garbage = fs::temp_directory_path() / "camera_iq_garbage.RAF";
  {
    std::ofstream os(garbage, std::ios::binary);
    os << "this is not a raw file";
  }
  check(!read_raw_metadata(garbage).has_value(), "garbage file yields nullopt");
  fs::remove(garbage);
}
