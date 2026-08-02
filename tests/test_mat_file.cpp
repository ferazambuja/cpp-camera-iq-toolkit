#include "camera_iq/mat_file.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "harness.hpp"

namespace {

// Minimal MATLAB v5 writer, used only to build inputs for these tests. Building
// the bytes here rather than committing a fixture keeps the tests hermetic and
// makes each field of the format explicit at the point it is exercised.

void put_u32(std::string& out, std::uint32_t v) {
  char b[4];
  std::memcpy(b, &v, 4);
  out.append(b, 4);
}

void pad_to_8(std::string& out) {
  while (out.size() % 8 != 0) out.push_back('\0');
}

// A MAT data element: 8-byte tag then the payload padded to an 8-byte boundary.
std::string element(std::uint32_t type, const std::string& payload) {
  std::string out;
  put_u32(out, type);
  put_u32(out, static_cast<std::uint32_t>(payload.size()));
  out += payload;
  pad_to_8(out);
  return out;
}

// MAT array class codes used here.
constexpr std::uint32_t kMxStruct = 2;
constexpr std::uint32_t kMxDouble = 6;

// MAT data type codes used here.
constexpr std::uint32_t kMiInt8 = 1;
constexpr std::uint32_t kMiUint16 = 4;
constexpr std::uint32_t kMiInt32 = 5;
constexpr std::uint32_t kMiUint32 = 6;
constexpr std::uint32_t kMiDouble = 9;
constexpr std::uint32_t kMiMatrix = 14;
constexpr std::uint32_t kMiCompressed = 15;

std::string zlib_deflate(const std::string& in) {
  uLongf cap = compressBound(static_cast<uLong>(in.size()));
  std::string out(cap, '\0');
  const int rc = compress(reinterpret_cast<Bytef*>(out.data()), &cap,
                          reinterpret_cast<const Bytef*>(in.data()),
                          static_cast<uLong>(in.size()));
  if (rc != Z_OK) throw std::runtime_error("test: deflate failed");
  out.resize(cap);
  return out;
}

std::string array_flags(std::uint32_t klass) {
  std::string payload;
  put_u32(payload, klass);  // class in the low byte, no flags set
  put_u32(payload, 0);
  return element(kMiUint32, payload);
}

std::string dimensions(const std::vector<std::int32_t>& dims) {
  std::string payload;
  for (const std::int32_t d : dims) {
    put_u32(payload, static_cast<std::uint32_t>(d));
  }
  return element(kMiInt32, payload);
}

std::string array_name(const std::string& name) {
  return element(kMiInt8, name);
}

std::string double_matrix(const std::string& name,
                          const std::vector<std::int32_t>& dims,
                          const std::vector<double>& values) {
  std::string payload;
  for (const double v : values) {
    char b[8];
    std::memcpy(b, &v, 8);
    payload.append(b, 8);
  }
  const std::string body = array_flags(kMxDouble) + dimensions(dims) +
                           array_name(name) + element(kMiDouble, payload);
  return element(kMiMatrix, body);
}

// `fields` are (name, already-encoded miMATRIX element) pairs.
std::string struct_matrix(
    const std::string& name,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  std::size_t max_len = 0;
  for (const auto& f : fields) max_len = std::max(max_len, f.first.size() + 1);

  std::string len_payload;
  put_u32(len_payload, static_cast<std::uint32_t>(max_len));

  std::string names;
  for (const auto& f : fields) {
    names += f.first;
    names.append(max_len - f.first.size(), '\0');
  }

  std::string body = array_flags(kMxStruct) + dimensions({1, 1}) +
                     array_name(name) + element(kMiInt32, len_payload) +
                     element(kMiInt8, names);
  for (const auto& f : fields) body += f.second;
  return element(kMiMatrix, body);
}

std::string mat_header() {
  std::string out(116, ' ');
  out.append(8, '\0');                  // subsystem data offset
  out.push_back('\0');                  // version, little-endian 0x0100
  out.push_back('\1');
  out.push_back('I');                   // endian indicator, "IM" = little
  out.push_back('M');
  return out;
}

}  // namespace

using test::check;
using test::check_near;

void TESTS() {
  // A file that does not carry the v5 endian indicator is not a MAT-file, and
  // must say so rather than producing an empty struct that reads as "no fields".
  {
    std::string not_a_mat(200, 'x');
    bool threw = false;
    try {
      camera_iq::read_mat_struct(not_a_mat);
    } catch (const std::runtime_error&) {
      threw = true;
    }
    check(threw, "mat: a buffer without the v5 endian indicator is rejected");
  }

  {
    std::string truncated = mat_header();
    truncated.resize(64);
    bool threw = false;
    try {
      camera_iq::read_mat_struct(truncated);
    } catch (const std::runtime_error&) {
      threw = true;
    }
    check(threw, "mat: a buffer shorter than the 128-byte header is rejected");
  }

  // The shape the archive actually stores: one struct variable whose fields are
  // numeric arrays. `wl` and `radiance` are the two this ingest reads.
  {
    const std::string file =
        mat_header() +
        struct_matrix("measurements",
                      {{"wl", double_matrix("", {1, 3}, {380, 382, 384})},
                       {"XYZ", double_matrix("", {1, 3}, {291.736, 297.603,
                                                          290.922})}});
    const auto s = camera_iq::read_mat_struct(file);
    check(s.size() == 2, "mat: struct exposes both fields");
    check(s.count("wl") == 1 && s.count("XYZ") == 1,
          "mat: fields are keyed by their declared names");
    check(s.at("wl").dims == std::vector<std::size_t>{1, 3},
          "mat: dimensions are preserved");
    check(s.at("wl").values == std::vector<double>{380, 382, 384},
          "mat: numeric values round-trip");
    check_near(s.at("XYZ").values.at(1), 297.603, 1e-9,
               "mat: a second field is read independently");
  }

  // Every file in the archive stores its whole payload as one miCOMPRESSED
  // element, so a reader that only handles uncompressed elements parses none of
  // them. Deflating here with zlib keeps the test hermetic: it builds the same
  // container MATLAB writes rather than depending on a committed fixture.
  {
    const std::string inner =
        struct_matrix("measurements",
                      {{"wl", double_matrix("", {1, 2}, {380, 382})}});
    const std::string file = mat_header() + element(kMiCompressed,
                                                    zlib_deflate(inner));
    const auto s = camera_iq::read_mat_struct(file);
    check(s.count("wl") == 1, "mat: a compressed payload is inflated");
    check(s.at("wl").values == std::vector<double>{380, 382},
          "mat: values survive the inflate path");
  }

  // Mixed widths in one struct, as the archive stores them: radiance as double,
  // the wavelength axis as uint16, the repeat counters as uint8.
  {
    std::string u16;
    for (const std::uint16_t v : {std::uint16_t{380}, std::uint16_t{382}}) {
      char b[2];
      std::memcpy(b, &v, 2);
      u16.append(b, 2);
    }
    const std::string wl_matrix = element(
        kMiMatrix, array_flags(kMxDouble) + dimensions({1, 2}) +
                       array_name("") + element(kMiUint16, u16));
    const std::string file =
        mat_header() +
        struct_matrix("measurements",
                      {{"wl", wl_matrix},
                       {"radiance", double_matrix("", {1, 2}, {0.5, 0.25})}});
    const auto s = camera_iq::read_mat_struct(file);
    check(s.at("wl").values == std::vector<double>{380, 382},
          "mat: a uint16 field widens to double");
    check(s.at("radiance").values == std::vector<double>{0.5, 0.25},
          "mat: a double field in the same struct is unaffected");
  }

  {
    // A struct is what this reader promises to return. A file holding only a
    // bare numeric variable must say so rather than returning no fields, which
    // a caller cannot distinguish from an empty struct.
    const std::string file =
        mat_header() + double_matrix("radiance", {1, 2}, {1.0, 2.0});
    bool threw = false;
    try {
      camera_iq::read_mat_struct(file);
    } catch (const std::runtime_error&) {
      threw = true;
    }
    check(threw, "mat: a file with no struct variable is rejected");
  }
}
