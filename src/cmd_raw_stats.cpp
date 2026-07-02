#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#include "camera_iq/json_writer.hpp"
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
  w.key("white_level");
  w.value(r.meta.white_level);
  w.key("raw_width");
  w.value(r.meta.raw_width);
  w.key("raw_height");
  w.value(r.meta.raw_height);
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
  std::filesystem::path out;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--out") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq raw-stats: --out requires a path\n";
        return 2;
      }
      out = argv[++i];
    } else if (file.empty()) {
      file = arg;
    } else {
      std::cerr << "camera_iq raw-stats: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }
  if (file.empty()) {
    std::cerr << "Usage: camera_iq raw-stats <raw-file> [--out FILE]\n";
    return 2;
  }

  const auto report = read_raw_cfa_stats(file);
  if (!report) {
    std::cerr << "camera_iq raw-stats: cannot read/unpack " << file << "\n";
    return 1;
  }

  if (out.empty()) {
    write_report_json(std::cout, file.string(), *report);
    std::cout << "\n";
  } else {
    std::ofstream os(out, std::ios::binary);
    if (!os) {
      std::cerr << "camera_iq raw-stats: cannot write " << out << "\n";
      return 1;
    }
    write_report_json(os, file.string(), *report);
    os << "\n";
    std::cerr << "raw-stats written to " << out << "\n";
  }
  return 0;
}

}  // namespace camera_iq
