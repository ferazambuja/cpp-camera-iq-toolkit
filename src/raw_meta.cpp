#include "camera_iq/raw_meta.hpp"

#include <libraw/libraw.h>

#include <array>
#include <cstddef>
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

std::array<double, 4> effective_black_levels(
    unsigned black, const unsigned* cblack, std::size_t cblack_len,
    const std::array<int, 4>& color_at_position) {
  // Tile dimensions live in cblack[4] (rows) and cblack[5] (cols); the tile
  // values start at cblack[6]. Positions (0,0)(0,1)(1,0)(1,1) in row/col terms.
  static constexpr int kRow[4] = {0, 0, 1, 1};
  static constexpr int kCol[4] = {0, 1, 0, 1};
  const unsigned bh = cblack_len > 4 ? cblack[4] : 0;
  const unsigned bw = cblack_len > 5 ? cblack[5] : 0;

  std::array<double, 4> out{0, 0, 0, 0};
  for (int p = 0; p < 4; ++p) {
    double eff = static_cast<double>(black);
    const int ch = color_at_position[static_cast<size_t>(p)];
    if (ch >= 0 && static_cast<std::size_t>(ch) < cblack_len) {
      eff += static_cast<double>(cblack[static_cast<size_t>(ch)]);
    }
    if (bh > 0 && bw > 0) {
      const std::size_t idx = 6u +
          (static_cast<unsigned>(kRow[p]) % bh) * bw +
          (static_cast<unsigned>(kCol[p]) % bw);
      if (idx < cblack_len) eff += static_cast<double>(cblack[idx]);
    }
    out[static_cast<size_t>(p)] = eff;
  }
  return out;
}

namespace {

// Fills RawMeta from an already-opened LibRaw processor. Metadata (including the
// cblack tile and COLOR() layout) is available after open_file(); no unpack.
RawMeta meta_from_processor(LibRaw& processor) {
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

  // COLOR() index of each top-left 2x2 position (accounts for crop margins);
  // reused for both the CFA string and the effective black computation.
  const std::array<int, 4> color_at_position = {
      processor.COLOR(0, 0), processor.COLOR(0, 1),
      processor.COLOR(1, 0), processor.COLOR(1, 1)};

  // Effective black must combine the scalar `black`, per-channel cblack[0..3],
  // and the cblack[6..] tile — the X-T100 stores its ~1024 DN pedestal in the
  // tile and leaves the scalar at 0, so reading `color.black` alone yields 0.
  // Tile phase assumes a zero-margin sensor (sizes.top_margin/left_margin == 0,
  // true for the X-T100); see effective_black_levels() for the cropped-sensor
  // caveat before trusting this on other cameras.
  meta.black_per_channel = effective_black_levels(
      color.black, color.cblack,
      sizeof(color.cblack) / sizeof(color.cblack[0]), color_at_position);
  meta.black_level = (meta.black_per_channel[0] + meta.black_per_channel[1] +
                      meta.black_per_channel[2] + meta.black_per_channel[3]) /
                     4.0;
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

  // CFA pattern of the visible-area top-left 2x2, from the COLOR() indices
  // computed above (which already account for the sensor crop margins).
  meta.cfa_pattern = cfa_pattern_string(idata.cdesc, color_at_position);

  return meta;
}

}  // namespace

std::optional<RawMeta> read_raw_metadata(const std::filesystem::path& raw) {
  LibRaw processor;
  if (processor.open_file(raw.string().c_str()) != LIBRAW_SUCCESS) {
    return std::nullopt;
  }
  return meta_from_processor(processor);
}

std::optional<RawCfaReport> read_raw_cfa_stats(
    const std::filesystem::path& raw) {
  LibRaw processor;
  if (processor.open_file(raw.string().c_str()) != LIBRAW_SUCCESS) {
    return std::nullopt;
  }
  RawCfaReport report;
  report.meta = meta_from_processor(processor);

  if (processor.unpack() != LIBRAW_SUCCESS) {
    return std::nullopt;
  }
  const std::uint16_t* image = processor.imgdata.rawdata.raw_image;
  if (image == nullptr) {
    return std::nullopt;  // no Bayer mosaic (X-Trans/Foveon/demosaiced input)
  }

  const auto& sizes = processor.imgdata.sizes;
  const auto& color = processor.imgdata.color;
  const std::array<int, 4> color_at_position = {
      processor.COLOR(0, 0), processor.COLOR(0, 1),
      processor.COLOR(1, 0), processor.COLOR(1, 1)};

  // Statistics over the full raw mosaic (zero-margin sensor assumed, as for the
  // black level), black-subtracted with the effective per-position black.
  report.planes = cfa_plane_stats(
      image, sizes.raw_width, sizes.raw_height, color_at_position,
      processor.imgdata.idata.cdesc, report.meta.black_per_channel,
      static_cast<double>(color.maximum));
  return report;
}

}  // namespace camera_iq
