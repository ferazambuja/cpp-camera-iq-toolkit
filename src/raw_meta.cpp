#include "camera_iq/raw_meta.hpp"

#include <libraw/libraw.h>

#include <cstdio>
#include <ctime>

namespace camera_iq {

std::string cfa_pattern_string(std::string_view cdesc,
                               const std::array<int, 4>& color_indices) {
  std::string pattern;
  pattern.reserve(4);
  for (int idx : color_indices) {
    if (idx < 0 || static_cast<size_t>(idx) >= cdesc.size()) return {};
    pattern += cdesc[static_cast<size_t>(idx)];
  }
  return pattern;
}

std::optional<RawMeta> read_raw_metadata(const std::filesystem::path& raw) {
  LibRaw processor;
  if (processor.open_file(raw.string().c_str()) != LIBRAW_SUCCESS) {
    return std::nullopt;
  }

  const auto& idata = processor.imgdata.idata;
  const auto& other = processor.imgdata.other;
  const auto& sizes = processor.imgdata.sizes;
  const auto& color = processor.imgdata.color;

  RawMeta meta;
  meta.make = idata.make;
  meta.model = idata.model;
  meta.iso = other.iso_speed;
  meta.shutter_s = other.shutter;
  meta.aperture = other.aperture;
  meta.focal_length_mm = other.focal_len;
  meta.raw_width = sizes.raw_width;
  meta.raw_height = sizes.raw_height;
  meta.black_level = color.black;
  for (int i = 0; i < 4; ++i) {
    meta.black_per_channel[static_cast<size_t>(i)] = color.cblack[i];
  }
  meta.white_level = color.maximum;

  if (other.timestamp != 0) {
    std::tm tm_buf{};
    if (localtime_r(&other.timestamp, &tm_buf) != nullptr) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                    tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                    tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
      meta.timestamp = buf;
    }
  }

  // CFA pattern of the visible-area top-left 2x2, via LibRaw's COLOR()
  // (which already accounts for the sensor crop margins).
  meta.cfa_pattern = cfa_pattern_string(
      idata.cdesc, {processor.COLOR(0, 0), processor.COLOR(0, 1),
                    processor.COLOR(1, 0), processor.COLOR(1, 1)});

  return meta;
}

}  // namespace camera_iq
