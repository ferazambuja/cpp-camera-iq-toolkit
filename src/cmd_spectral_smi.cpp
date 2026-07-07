#include "camera_iq/commands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/spectral_smi.hpp"

namespace camera_iq {
namespace {

double to_double(const std::string& s, const std::string& ctx) {
  try {
    std::size_t used = 0;
    const double v = std::stod(s, &used);
    if (used == 0 || !std::isfinite(v)) throw std::runtime_error("");
    return v;
  } catch (...) {
    throw std::runtime_error("spectral smi: bad number in " + ctx + ": '" + s +
                             "'");
  }
}

std::vector<std::string> split_line(const std::string& ln) {
  std::vector<std::string> f;
  std::string cur;
  for (const char c : ln) {
    if (c == ',' || c == '\t') {
      f.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  f.push_back(cur);
  return f;
}

// Applies flush() to each CR/LF/CRLF-delimited line, skipping the header.
template <typename Flush>
void for_each_data_line(const std::filesystem::path& path, Flush flush) {
  std::ifstream is(path, std::ios::binary);
  if (!is)
    throw std::runtime_error("spectral smi: cannot open " + path.string());
  std::string all((std::istreambuf_iterator<char>(is)),
                  std::istreambuf_iterator<char>());
  std::string line;
  bool header = true;
  auto emit = [&](const std::string& ln) {
    if (header) {
      header = false;
      return;
    }
    if (!ln.empty()) flush(ln);
  };
  for (std::size_t i = 0; i < all.size(); ++i) {
    const char c = all[i];
    if (c == '\n') {
      emit(line);
      line.clear();
    } else if (c == '\r') {
      emit(line);
      line.clear();
      if (i + 1 < all.size() && all[i + 1] == '\n') ++i;
    } else {
      line += c;
    }
  }
  if (!line.empty()) emit(line);
}

// "Wavelength (nm),A,B,C" -> rounded-int wavelength -> (a,b,c).
std::map<int, std::array<double, 3>> read_triple_csv(
    const std::filesystem::path& path) {
  std::map<int, std::array<double, 3>> out;
  for_each_data_line(path, [&](const std::string& ln) {
    const auto f = split_line(ln);
    if (f.size() < 4) return;
    out[static_cast<int>(std::llround(to_double(f[0], "wavelength")))] = {
        to_double(f[1], "col1"), to_double(f[2], "col2"),
        to_double(f[3], "col3")};
  });
  if (out.empty())
    throw std::runtime_error("spectral smi: empty triple CSV " + path.string());
  return out;
}

// "Wavelength (nm),Power" -> rounded-int wavelength -> power.
std::map<int, double> read_pair_csv(const std::filesystem::path& path) {
  std::map<int, double> out;
  for_each_data_line(path, [&](const std::string& ln) {
    const auto f = split_line(ln);
    if (f.size() < 2) return;
    out[static_cast<int>(std::llround(to_double(f[0], "wavelength")))] =
        to_double(f[1], "power");
  });
  if (out.empty())
    throw std::runtime_error("spectral smi: empty illuminant CSV " +
                             path.string());
  return out;
}

std::string arg_value(int argc, char** argv, int& i) {
  if (i + 1 >= argc)
    throw std::runtime_error(std::string("spectral smi: missing value for ") +
                             argv[i]);
  return argv[++i];
}

}  // namespace

int cmd_spectral_smi(int argc, char** argv) {
  std::string ssf_csv, cmf_csv, illum_csv, refl_csv, refl_cgats, camera_model,
      out_path;
  double smi_slope = 5.5;
  try {
    for (int i = 0; i < argc; ++i) {
      const std::string a = argv[i];
      if (a == "--ssf-csv") ssf_csv = arg_value(argc, argv, i);
      else if (a == "--cmf") cmf_csv = arg_value(argc, argv, i);
      else if (a == "--illuminant-csv") illum_csv = arg_value(argc, argv, i);
      else if (a == "--reflectance-csv") refl_csv = arg_value(argc, argv, i);
      else if (a == "--reflectance-cgats") refl_cgats = arg_value(argc, argv, i);
      else if (a == "--camera-model") camera_model = arg_value(argc, argv, i);
      else if (a == "--smi-slope")
        smi_slope = to_double(arg_value(argc, argv, i), "--smi-slope");
      else if (a == "--out") out_path = arg_value(argc, argv, i);
      else {
        std::cerr << "spectral smi: unknown argument " << a << "\n";
        return 2;
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "camera_iq spectral-smi: " << e.what() << "\n";
    return 2;
  }

  if (ssf_csv.empty() || cmf_csv.empty() || illum_csv.empty() ||
      camera_model.empty() || (refl_csv.empty() && refl_cgats.empty())) {
    std::cerr
        << "Usage: camera_iq spectral-smi --ssf-csv F --cmf F "
           "--illuminant-csv F\n"
           "                              (--reflectance-csv F | "
           "--reflectance-cgats F)\n"
           "                              --camera-model N [--smi-slope 5.5] "
           "[--out F]\n"
           "Camera Sensitivity Metamerism Index (ISO 17321 style): fits the\n"
           "optimal 3x3 RGB->XYZ transform for the test colours under the\n"
           "reference illuminant and reports SMI = 100 - slope * mean CIELAB\n"
           "error. Higher SMI is better; 100 is a Luther-condition camera.\n";
    return 2;
  }

  try {
    const auto ssf = read_triple_csv(ssf_csv);
    const auto cmf = read_triple_csv(cmf_csv);
    const auto illum = read_pair_csv(illum_csv);
    const SpectralReference ref =
        !refl_csv.empty() ? read_spectral_reference_csv(refl_csv)
                          : read_spectral_reference_cgats(refl_cgats);

    // Reflectance wavelength -> column index in the reference table.
    std::map<int, std::size_t> refl_index;
    for (std::size_t i = 0; i < ref.wavelengths_nm.size(); ++i)
      refl_index[static_cast<int>(std::llround(ref.wavelengths_nm[i]))] = i;

    // Shared grid = wavelengths present in all four spectral sources.
    std::vector<int> grid;
    for (const auto& [w, _] : cmf) {
      if (ssf.count(w) && illum.count(w) && refl_index.count(w))
        grid.push_back(w);
    }
    std::sort(grid.begin(), grid.end());
    if (grid.size() < 4)
      throw std::runtime_error(
          "spectral smi: SSF, CMF, illuminant and reflectance share fewer "
          "than four wavelengths");

    SpectralSmiInputs in;
    in.smi_slope = smi_slope;
    for (int w : grid) {
      in.grid_nm.push_back(w);
      in.illuminant.push_back(illum.at(w));
      const auto& s = ssf.at(w);
      const auto& c = cmf.at(w);
      for (int ch = 0; ch < 3; ++ch) {
        in.ssf[static_cast<std::size_t>(ch)].push_back(s[static_cast<std::size_t>(ch)]);
        in.cmf[static_cast<std::size_t>(ch)].push_back(c[static_cast<std::size_t>(ch)]);
      }
    }
    for (const auto& patch : ref.patches) {
      std::vector<double> refl;
      refl.reserve(grid.size());
      for (int w : grid) refl.push_back(patch.reflectance[refl_index.at(w)]);
      in.reflectance.push_back(std::move(refl));
    }

    const SpectralSmiResult res = compute_spectral_smi(in);

    std::ostringstream os;
    JsonWriter w(os);
    w.begin_object();
    w.key("method"); w.value(res.method);
    w.key("camera_model"); w.value(camera_model);
    w.key("reference_illuminant"); w.value(std::filesystem::path(illum_csv).filename().string());
    w.key("test_colour_source");
    w.value(std::filesystem::path(!refl_csv.empty() ? refl_csv : refl_cgats)
                .filename()
                .string());
    w.key("patch_count"); w.value(static_cast<std::int64_t>(res.patch_count));
    w.key("grid_first_nm"); w.value(static_cast<std::int64_t>(grid.front()));
    w.key("grid_last_nm"); w.value(static_cast<std::int64_t>(grid.back()));
    w.key("grid_size"); w.value(static_cast<std::int64_t>(grid.size()));
    w.key("mean_delta_e_76"); w.value(res.mean_delta_e_76);
    w.key("max_delta_e_76"); w.value(res.max_delta_e_76);
    w.key("rms_delta_e_76"); w.value(res.rms_delta_e_76);
    w.key("mean_delta_e_2000"); w.value(res.mean_delta_e_2000);
    w.key("smi_slope"); w.value(res.smi_slope);
    w.key("smi"); w.value(res.smi);
    w.key("matrix");
    w.begin_array();
    for (const auto& row : res.matrix) {
      w.begin_array();
      for (double m : row) w.value(m);
      w.end_array();
    }
    w.end_array();
    w.key("interpretation");
    w.value(
        "SMI = 100 - slope * mean CIELAB(1976) error after the optimal 3x3 "
        "RGB->XYZ fit over the test colours under the reference illuminant; "
        "higher is better, 100 is a Luther-condition camera. Slope and test "
        "set follow the ISO 17321 form; verify the exact constant and colour "
        "set against the standard before citing an absolute SMI.");
    w.end_object();

    const std::string json = os.str();
    if (!out_path.empty()) {
      std::ofstream ofs(out_path, std::ios::binary);
      ofs << json << "\n";
    } else {
      std::cout << json << "\n";
    }
    std::cerr << "spectral smi: " << camera_model << " smi=" << res.smi
              << " mean_delta_e_76=" << res.mean_delta_e_76 << " over "
              << res.patch_count << " colours\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "camera_iq spectral-smi: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
