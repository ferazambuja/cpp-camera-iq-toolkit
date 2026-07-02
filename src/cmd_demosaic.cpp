#include "camera_iq/commands.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "camera_iq/demosaic.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {
namespace {

void write_camera_json(JsonWriter& w, const RawMeta& meta) {
  w.key("camera");
  w.begin_object();
  w.key("make");
  w.value(meta.make);
  w.key("model");
  w.value(meta.model);
  w.key("cfa_pattern");
  w.value(meta.cfa_pattern);
  w.key("iso");
  w.value(meta.iso);
  w.key("shutter_s");
  w.value(meta.shutter_s);
  w.key("aperture");
  w.value(meta.aperture);
  w.key("black_level");
  w.value(meta.black_level);
  w.key("black_per_channel");
  w.begin_array();
  for (double b : meta.black_per_channel) w.value(b);
  w.end_array();
  w.key("white_level");
  w.value(meta.white_level);
  w.key("raw_width");
  w.value(meta.raw_width);
  w.key("raw_height");
  w.value(meta.raw_height);
  w.key("visible_width");
  w.value(meta.visible_width);
  w.key("visible_height");
  w.value(meta.visible_height);
  w.key("top_margin");
  w.value(meta.top_margin);
  w.key("left_margin");
  w.value(meta.left_margin);
  w.key("raw_pitch_bytes");
  w.value(meta.raw_pitch_bytes);
  w.end_object();
}

void write_channel_stats(JsonWriter& w, const std::array<ChannelStats, 3>& stats) {
  w.key("channels");
  w.begin_array();
  for (const auto& s : stats) {
    w.begin_object();
    w.key("channel");
    w.value(s.label);
    w.key("count");
    w.value(static_cast<std::int64_t>(s.count));
    w.key("min");
    w.value(s.min);
    w.key("max");
    w.value(s.max);
    w.key("mean");
    w.value(s.mean);
    w.key("stddev");
    w.value(s.stddev);
    w.end_object();
  }
  w.end_array();
}

void write_report_json(std::ostream& os, const std::string& file,
                       const RawCfaImage& cfa,
                       const std::array<ChannelStats, 3>& stats) {
  JsonWriter w(os);
  w.begin_object();
  w.key("file");
  w.value(file);
  write_camera_json(w, cfa.meta);
  w.key("image");
  w.begin_object();
  w.key("width");
  w.value(cfa.width);
  w.key("height");
  w.value(cfa.height);
  w.key("pixels");
  w.value(static_cast<std::int64_t>(cfa.width) *
          static_cast<std::int64_t>(cfa.height));
  w.key("algorithm");
  w.value("bilinear");
  w.key("output");
  w.value("summary_stats_only");
  w.end_object();
  write_channel_stats(w, stats);
  w.end_object();
}

}  // namespace

int cmd_demosaic(int argc, char** argv) {
  std::filesystem::path file;
  std::filesystem::path out;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq demosaic: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (file.empty()) {
      file = arg;
    } else {
      std::cerr << "camera_iq demosaic: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }
  if (file.empty()) {
    std::cerr << "Usage: camera_iq demosaic <raw-file> [--out FILE]\n";
    return 2;
  }

  const auto cfa = read_raw_cfa_image(file);
  if (!cfa) {
    std::cerr << "camera_iq demosaic: cannot read/unpack " << file << "\n";
    return 1;
  }
  const auto rgb = demosaic_bilinear(cfa->samples.data(), cfa->width,
                                     cfa->height, cfa->row_stride_pixels,
                                     cfa->color_at_position, cfa->cdesc);
  if (rgb.empty()) {
    std::cerr << "camera_iq demosaic: unsupported CFA descriptor for "
              << file << "\n";
    return 1;
  }
  const auto stats = rgb_image_stats(rgb);

  if (out.empty()) {
    write_report_json(std::cout, file.string(), *cfa, stats);
    std::cout << "\n";
  } else {
    std::ofstream os(out, std::ios::binary);
    if (!os) {
      std::cerr << "camera_iq demosaic: cannot write " << out << "\n";
      return 1;
    }
    write_report_json(os, file.string(), *cfa, stats);
    os << "\n";
    std::cerr << "demosaic summary written to " << out << "\n";
  }
  return 0;
}

}  // namespace camera_iq
