#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "camera_iq/dataset_config.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {
namespace {

void write_report_json(std::ostream& os, const std::string& file,
                       const RawCfaReport& r) {
  JsonWriter w(os);
  w.begin_object();
  w.key("file");
  w.value(file);

  w.key("camera");
  w.begin_object();
  w.key("make");
  w.value(r.meta.make);
  w.key("model");
  w.value(r.meta.model);
  w.key("cfa_pattern");
  w.value(r.meta.cfa_pattern);
  w.key("iso");
  w.value(r.meta.iso);
  w.key("shutter_s");
  w.value(r.meta.shutter_s);
  w.key("aperture");
  w.value(r.meta.aperture);
  w.key("black_level");
  w.value(r.meta.black_level);
  w.key("black_per_channel");
  w.begin_array();
  for (double b : r.meta.black_per_channel) w.value(b);
  w.end_array();
  w.key("white_level");
  w.value(r.meta.white_level);
  w.key("raw_width");
  w.value(r.meta.raw_width);
  w.key("raw_height");
  w.value(r.meta.raw_height);
  w.key("visible_width");
  w.value(r.meta.visible_width);
  w.key("visible_height");
  w.value(r.meta.visible_height);
  w.key("top_margin");
  w.value(r.meta.top_margin);
  w.key("left_margin");
  w.value(r.meta.left_margin);
  w.key("raw_pitch_bytes");
  w.value(r.meta.raw_pitch_bytes);
  w.end_object();

  w.key("planes");
  w.begin_array();
  for (const auto& p : r.planes) {
    w.begin_object();
    w.key("channel");
    w.value(p.label);
    w.key("count");
    w.value(static_cast<std::int64_t>(p.count));
    w.key("min");
    w.value(p.min);
    w.key("max");
    w.value(p.max);
    w.key("mean");
    w.value(p.mean);
    w.key("stddev");
    w.value(p.stddev);
    w.key("below_black_fraction");
    w.value(p.below_black_fraction);
    w.key("saturated_fraction");
    w.value(p.saturated_fraction);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

}  // namespace

int cmd_raw_stats(int argc, char** argv) {
  std::filesystem::path file;
  std::string dataset_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path out;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq raw-stats: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (arg == "--dataset") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq raw-stats: --dataset requires an id\n";
        return 2;
      }
      dataset_id = argv[++i];
    } else if (arg == "--config") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq raw-stats: --config requires a path\n";
        return 2;
      }
      config = argv[++i];
    } else if (file.empty()) {
      file = arg;
    } else {
      std::cerr << "camera_iq raw-stats: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }
  if (file.empty()) {
    std::cerr << "Usage: camera_iq raw-stats <raw-file> [--dataset ID]"
                 " [--config FILE] [--out FILE]\n";
    return 2;
  }

  std::filesystem::path actual_file = file;
  std::string file_label = file.string();
  if (!dataset_id.empty()) {
    const auto resolved = resolve_dataset_root(dataset_id, config);
    if (!resolved || !resolved->from_config) {
      std::cerr << "camera_iq raw-stats: dataset id '" << dataset_id
                << "' not found in " << config << "\n";
      return 1;
    }
    if (file.is_absolute()) {
      std::cerr << "camera_iq raw-stats: --dataset requires a relative file\n";
      return 2;
    }
    actual_file = resolved->root / file;
    file_label = dataset_file_label(dataset_id, file);
  }

  const auto report = read_raw_cfa_stats(actual_file);
  if (!report) {
    std::cerr << "camera_iq raw-stats: cannot read/unpack " << file_label << "\n";
    return 1;
  }

  if (out.empty()) {
    write_report_json(std::cout, file_label, *report);
    std::cout << "\n";
  } else {
    if (!write_output_file_checked(
            out, "raw-stats",
            [&](std::ostream& os) { write_report_json(os, file_label, *report); },
            std::cerr)) {
      return 1;
    }
    std::cerr << "raw-stats written to " << out << "\n";
  }
  return 0;
}

}  // namespace camera_iq
