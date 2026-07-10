#include "camera_iq/commands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"
#include "camera_iq/spectral_quality.hpp"

namespace camera_iq {
namespace {

double to_double(const std::string& s, const std::string& ctx) {
  try {
    std::size_t used = 0;
    const double v = std::stod(s, &used);
    if (used == 0 || !std::isfinite(v)) throw std::runtime_error("");
    return v;
  } catch (...) {
    throw std::runtime_error("spectral quality: bad number in " + ctx + ": '" +
                             s + "'");
  }
}

// "Wavelength (nm),A,B,C" with any of CR/LF/CRLF; returns wl -> (a,b,c).
std::map<int, std::array<double, 3>> read_triple_csv(
    const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is)
    throw std::runtime_error("spectral quality: cannot open " + path.string());
  std::string all((std::istreambuf_iterator<char>(is)),
                  std::istreambuf_iterator<char>());
  std::map<int, std::array<double, 3>> out;
  std::string line;
  bool header = true;
  auto flush = [&](const std::string& ln) {
    if (header) { header = false; return; }
    if (ln.empty()) return;
    std::vector<std::string> f;
    std::string cur;
    for (const char c : ln) {
      if (c == ',' || c == '\t') { f.push_back(cur); cur.clear(); }
      else cur += c;
    }
    f.push_back(cur);
    if (f.size() < 4) return;
    out[static_cast<int>(std::llround(to_double(f[0], "wavelength")))] = {
        to_double(f[1], "col1"), to_double(f[2], "col2"),
        to_double(f[3], "col3")};
  };
  for (std::size_t i = 0; i < all.size(); ++i) {
    const char c = all[i];
    if (c == '\n') { flush(line); line.clear(); }
    else if (c == '\r') {
      flush(line); line.clear();
      if (i + 1 < all.size() && all[i + 1] == '\n') ++i;
    } else line += c;
  }
  if (!line.empty()) flush(line);
  if (out.empty()) throw std::runtime_error("spectral quality: empty CSV " +
                                            path.string());
  return out;
}

std::string arg_value(int argc, char** argv, int& i) {
  if (i + 1 >= argc)
    throw std::runtime_error(std::string("spectral quality: missing value for ") +
                             argv[i]);
  return argv[++i];
}

}  // namespace

int cmd_spectral_quality(int argc, char** argv) {
  std::string ssf_csv, cmf_csv, camera_model, out_path;
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ssf-csv") ssf_csv = arg_value(argc, argv, i);
    else if (a == "--cmf") cmf_csv = arg_value(argc, argv, i);
    else if (a == "--camera-model") camera_model = arg_value(argc, argv, i);
    else if (a == "--out") out_path = arg_value(argc, argv, i);
    else {
      std::cerr << "spectral quality: unknown argument " << a << "\n";
      return 2;
    }
  }
  if (ssf_csv.empty() || cmf_csv.empty() || camera_model.empty()) {
    std::cerr << "Usage: camera_iq spectral-quality --ssf-csv F --cmf F "
                 "--camera-model N [--out F]\n"
                 "Luther-condition colour quality: fits the CIE CMFs from the "
                 "camera SSFs; lower residual is better. Per-camera, so it is a "
                 "fair cross-camera ranking (unlike the closure residual).\n";
    return 2;
  }

  try {
    const auto ssf = read_triple_csv(ssf_csv);
    const auto cmf = read_triple_csv(cmf_csv);

    std::vector<int> grid;
    for (const auto& [w, _] : cmf)
      if (ssf.find(w) != ssf.end()) grid.push_back(w);
    std::sort(grid.begin(), grid.end());
    if (grid.size() < 4)
      throw std::runtime_error(
          "spectral quality: SSF and CMF share fewer than 4 wavelengths");

    SpectralQualityInputs in;
    in.grid_nm = grid;
    for (int w : grid) {
      const auto& s = ssf.at(w);
      const auto& c = cmf.at(w);
      for (int ch = 0; ch < 3; ++ch) {
        in.ssf[static_cast<std::size_t>(ch)].push_back(s[static_cast<std::size_t>(ch)]);
        in.cmf[static_cast<std::size_t>(ch)].push_back(c[static_cast<std::size_t>(ch)]);
      }
    }

    const auto res = compute_spectral_quality(in);

    std::ostringstream os;
    JsonWriter w(os);
    w.begin_object();
    w.key("method"); w.value(res.method);
    w.key("camera_model"); w.value(camera_model);
    w.key("grid_nm");
    w.begin_array();
    for (int g : grid) w.value(static_cast<std::int64_t>(g));
    w.end_array();
    w.key("cmf_residual");
    w.begin_object();
    w.key("x"); w.value(res.cmf_residual[0]);
    w.key("y"); w.value(res.cmf_residual[1]);
    w.key("z"); w.value(res.cmf_residual[2]);
    w.end_object();
    w.key("combined_residual"); w.value(res.combined_residual);
    w.key("quality_index"); w.value(res.quality_index);
    w.key("interpretation");
    w.value(
        "lower combined_residual = SSFs better span the CIE visual subspace "
        "(Luther) = better colour fidelity; this is an SSF property, not a "
        "closure or capture metric");
    w.end_object();

    const std::string json = os.str();
    if (!out_path.empty()) {
      if (!write_output_file_checked(
              out_path, "spectral-quality",
              [&](std::ostream& ofs) { ofs << json; }, std::cerr)) {
        return 1;
      }
    } else {
      std::cout << json << "\n";
    }
    std::cerr << "spectral quality: " << camera_model
              << " combined_residual=" << res.combined_residual
              << " quality_index=" << res.quality_index << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "camera_iq spectral-quality: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
