#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

namespace spectro_test {

inline void put_u32(std::string &out, std::uint32_t value) {
  char bytes[4];
  std::memcpy(bytes, &value, 4);
  out.append(bytes, 4);
}

inline void pad_to_8(std::string &out) {
  while (out.size() % 8 != 0)
    out.push_back('\0');
}

inline std::string element(std::uint32_t type, const std::string &payload) {
  std::string out;
  put_u32(out, type);
  put_u32(out, static_cast<std::uint32_t>(payload.size()));
  out += payload;
  pad_to_8(out);
  return out;
}

constexpr std::uint32_t kMxStruct = 2;
constexpr std::uint32_t kMxDouble = 6;
constexpr std::uint32_t kMxUint8 = 9;
constexpr std::uint32_t kMiInt8 = 1;
constexpr std::uint32_t kMiUint8 = 2;
constexpr std::uint32_t kMiInt32 = 5;
constexpr std::uint32_t kMiUint32 = 6;
constexpr std::uint32_t kMiDouble = 9;
constexpr std::uint32_t kMiMatrix = 14;
constexpr std::uint32_t kMiCompressed = 15;

inline std::string array_flags(std::uint32_t klass, std::uint32_t flags = 0) {
  std::string payload;
  put_u32(payload, klass | flags);
  put_u32(payload, 0);
  return element(kMiUint32, payload);
}

inline std::string dimensions(const std::vector<std::int32_t> &dims) {
  std::string payload;
  for (const std::int32_t dimension : dims) {
    put_u32(payload, static_cast<std::uint32_t>(dimension));
  }
  return element(kMiInt32, payload);
}

inline std::string array_name(const std::string &name) {
  return element(kMiInt8, name);
}

inline std::string double_matrix(const std::string &name,
                                 const std::vector<std::int32_t> &dims,
                                 const std::vector<double> &values) {
  std::string payload;
  for (const double value : values) {
    char bytes[8];
    std::memcpy(bytes, &value, 8);
    payload.append(bytes, 8);
  }
  const std::string body = array_flags(kMxDouble) + dimensions(dims) +
                           array_name(name) + element(kMiDouble, payload);
  return element(kMiMatrix, body);
}

inline std::string logical_matrix(const std::string &name, bool value) {
  const std::string payload(1, value ? '\1' : '\0');
  const std::string body = array_flags(kMxUint8, 0x0200u) + dimensions({1, 1}) +
                           array_name(name) + element(kMiUint8, payload);
  return element(kMiMatrix, body);
}

inline std::string
struct_matrix(const std::string &name,
              const std::vector<std::pair<std::string, std::string>> &fields) {
  std::size_t field_width = 0;
  for (const auto &field : fields) {
    field_width = std::max(field_width, field.first.size() + 1);
  }
  std::string width_payload;
  put_u32(width_payload, static_cast<std::uint32_t>(field_width));
  std::string names;
  for (const auto &field : fields) {
    names += field.first;
    names.append(field_width - field.first.size(), '\0');
  }
  std::string body = array_flags(kMxStruct) + dimensions({1, 1}) +
                     array_name(name) + element(kMiInt32, width_payload) +
                     element(kMiInt8, names);
  for (const auto &field : fields)
    body += field.second;
  return element(kMiMatrix, body);
}

inline std::string measurement_mat(double radiance_offset = 0.0) {
  std::string header(116, ' ');
  header.append(8, '\0');
  header.push_back('\0');
  header.push_back('\1');
  header.push_back('I');
  header.push_back('M');
  return header +
         struct_matrix(
             "measurements",
             {{"wl", double_matrix("", {1, 3}, {380, 382, 384})},
              {"radiance",
               double_matrix("", {1, 3},
                             {1.0 + radiance_offset, 2.0 + radiance_offset,
                              3.0 + radiance_offset})},
              {"XYZ", double_matrix("", {1, 3}, {10, 20, 30})},
              {"totalRadiance", double_matrix("", {1, 1}, {12})},
              {"CCT", double_matrix("", {1, 1}, {5500})},
              {"Duv", double_matrix("", {1, 1}, {-0.001})},
              {"repeatOnError", logical_matrix("", true)},
              {"numCurrentRepetitions", double_matrix("", {1, 1}, {0})}});
}

inline std::string compressed_measurement_mat(double radiance_offset = 0.0) {
  const std::string uncompressed = measurement_mat(radiance_offset);
  const std::string payload = uncompressed.substr(128);
  uLongf compressed_size = compressBound(static_cast<uLong>(payload.size()));
  std::string compressed(static_cast<std::size_t>(compressed_size), '\0');
  if (compress2(reinterpret_cast<Bytef *>(compressed.data()), &compressed_size,
                reinterpret_cast<const Bytef *>(payload.data()),
                static_cast<uLong>(payload.size()), Z_BEST_SPEED) != Z_OK) {
    throw std::runtime_error("spectro fixture: zlib compression failed");
  }
  compressed.resize(static_cast<std::size_t>(compressed_size));
  std::string result = uncompressed.substr(0, 128);
  put_u32(result, kMiCompressed);
  put_u32(result, static_cast<std::uint32_t>(compressed.size()));
  result += compressed;
  return result;
}

} // namespace spectro_test
