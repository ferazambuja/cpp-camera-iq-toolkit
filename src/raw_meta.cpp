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
  // values start at cblack[6]. LibRaw applies this pattern in visible-image
  // coordinates after margins are cropped, so origin is always active-area (0,0).
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

bool is_supported_bayer_filter(unsigned filters) {
  return filters >= 1000u;
}

int effective_raw_stride_pixels(unsigned raw_pitch_bytes, unsigned raw_width) {
  if (raw_pitch_bytes == 0) {
    return static_cast<int>(raw_width);
  }
  if (raw_pitch_bytes % sizeof(std::uint16_t) != 0) {
    return 0;
  }
  return static_cast<int>(raw_pitch_bytes / sizeof(std::uint16_t));
}

namespace {

// Fills RawMeta from the processor's current state. Some makers populate black
// level and raw pitch during unpack(), so callers that need pixel-correct stats
// must call this after unpack().
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
  meta.visible_width = sizes.width;
  meta.visible_height = sizes.height;
  meta.top_margin = sizes.top_margin;
  meta.left_margin = sizes.left_margin;
  const int effective_stride =
      effective_raw_stride_pixels(sizes.raw_pitch, sizes.raw_width);
  meta.raw_pitch_bytes =
      effective_stride > 0 ? effective_stride * static_cast<int>(sizeof(std::uint16_t))
                           : static_cast<int>(sizes.raw_pitch);

  // COLOR() index of each top-left 2x2 position (accounts for crop margins);
  // reused for both the CFA string and the effective black computation.
  const std::array<int, 4> color_at_position = {
      processor.COLOR(0, 0), processor.COLOR(0, 1),
      processor.COLOR(1, 0), processor.COLOR(1, 1)};

  // Effective black must combine the scalar `black`, per-channel cblack[0..3],
  // and the cblack[6..] tile — the X-T100 stores its ~1024 DN pedestal in the
  // tile and leaves the scalar at 0, so reading `color.black` alone yields 0.
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
      if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf) > 0) {
        meta.timestamp = buf;
      }
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

  if (!is_supported_bayer_filter(processor.imgdata.idata.filters)) {
    return std::nullopt;
  }

  if (processor.unpack() != LIBRAW_SUCCESS) {
    return std::nullopt;
  }
  report.meta = meta_from_processor(processor);

  const std::uint16_t* image = processor.imgdata.rawdata.raw_image;
  if (image == nullptr) {
    return std::nullopt;  // no Bayer mosaic (X-Trans/Foveon/demosaiced input)
  }

  const auto& sizes = processor.imgdata.sizes;
  const auto& color = processor.imgdata.color;
  if (sizes.width <= 0 || sizes.height <= 0) {
    return std::nullopt;
  }
  const int raw_stride_pixels =
      effective_raw_stride_pixels(sizes.raw_pitch, sizes.raw_width);
  if (raw_stride_pixels <= 0 ||
      static_cast<int>(sizes.left_margin) + static_cast<int>(sizes.width) >
          raw_stride_pixels ||
      static_cast<int>(sizes.top_margin) + static_cast<int>(sizes.height) >
          static_cast<int>(sizes.raw_height)) {
    return std::nullopt;
  }
  const std::array<int, 4> color_at_position = {
      processor.COLOR(0, 0), processor.COLOR(0, 1),
      processor.COLOR(1, 0), processor.COLOR(1, 1)};

  // Statistics over the visible active area. raw_image is full-sensor data with
  // masked frame pixels still present, so stride through raw_pitch and start at
  // top_margin/left_margin.
  const std::uint16_t* active =
      image + static_cast<std::size_t>(sizes.top_margin) *
                  static_cast<std::size_t>(raw_stride_pixels) +
      static_cast<std::size_t>(sizes.left_margin);
  report.planes = cfa_plane_stats_strided(
      active, sizes.width, sizes.height, raw_stride_pixels, color_at_position,
      processor.imgdata.idata.cdesc, report.meta.black_per_channel,
      static_cast<double>(color.maximum));
  return report;
}

}  // namespace camera_iq
