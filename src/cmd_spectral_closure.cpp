#include "camera_iq/commands.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "camera_iq/json_writer.hpp"
#include "camera_iq/spectral_closure.hpp"

namespace camera_iq {
namespace {

std::vector<std::string> split_ws(const std::string& line) {
  std::vector<std::string> out;
  std::string field;
  for (const char c : line) {
    if (c == '\t' || c == ',' || c == '\r') {
      if (!field.empty()) { out.push_back(field); field.clear(); }
    } else if (c == ' ') {
      // keep spaces inside fields only if no tab/comma delimiter seen; SG
      // sidecars are tab-delimited, so treat runs of spaces as separators too.
      if (!field.empty()) { out.push_back(field); field.clear(); }
    } else {
      field += c;
    }
  }
  if (!field.empty()) out.push_back(field);
  return out;
}

double to_double(const std::string& s, const std::string& ctx) {
  try {
    std::size_t used = 0;
    const double v = std::stod(s, &used);
    if (used == 0 || !std::isfinite(v)) throw std::runtime_error("");
    return v;
  } catch (...) {
    throw std::runtime_error("spectral closure: bad number in " + ctx + ": '" +
                             s + "'");
  }
}

// SSF CSV: "Wavelength (nm),Red,Green,Blue" then rows. Returns wl->(r,g,b).
std::map<int, std::array<double, 3>> read_ssf_csv(
    const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("spectral closure: cannot open SSF " +
                                    path.string());
  std::map<int, std::array<double, 3>> ssf;
  std::string line;
  bool header = true;
  while (std::getline(is, line)) {
    if (header) { header = false; continue; }
    if (line.empty() || line == "\r") continue;
    const auto f = split_ws(line);
    if (f.size() < 4) continue;
    const int wl = static_cast<int>(std::llround(to_double(f[0], "SSF wl")));
    ssf[wl] = {to_double(f[1], "SSF R"), to_double(f[2], "SSF G"),
               to_double(f[3], "SSF B")};
  }
  if (ssf.empty()) throw std::runtime_error("spectral closure: empty SSF CSV");
  return ssf;
}

// Illuminant: header line then "wavelength<sep>value". Linear-interpolatable.
std::vector<std::pair<double, double>> read_illuminant(
    const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("spectral closure: cannot open illuminant " +
                                    path.string());
  std::vector<std::pair<double, double>> spd;
  std::string line;
  bool header = true;
  while (std::getline(is, line)) {
    const auto f = split_ws(line);
    if (f.size() < 2) continue;
    try {
      const double wl = to_double(f[0], "illuminant wl");
      const double v = to_double(f[1], "illuminant value");
      spd.emplace_back(wl, v);
    } catch (...) {
      if (header) { header = false; continue; }  // skip a non-numeric header
      throw;
    }
    header = false;
  }
  if (spd.size() < 2)
    throw std::runtime_error("spectral closure: illuminant needs >= 2 samples");
  std::sort(spd.begin(), spd.end());
  return spd;
}

double interp(const std::vector<std::pair<double, double>>& spd, double wl) {
  if (wl <= spd.front().first) return spd.front().second;
  if (wl >= spd.back().first) return spd.back().second;
  for (std::size_t i = 0; i + 1 < spd.size(); ++i) {
    if (spd[i].first <= wl && wl <= spd[i + 1].first) {
      const double t =
          (wl - spd[i].first) / (spd[i + 1].first - spd[i].first);
      return spd[i].second + t * (spd[i + 1].second - spd[i].second);
    }
  }
  return 0.0;
}

struct CgatsRgb {
  std::map<std::string, std::array<double, 3>> rows;
  bool has_oe_levels = false;
  std::array<double, 3> oe_levels{0, 0, 0};
};

bool parse_oe_levels(const std::string& line, std::array<double, 3>& oe) {
  const auto pos = line.find("OELevels=");
  if (pos == std::string::npos) return false;
  std::string values;
  for (std::size_t i = pos + 9; i < line.size(); ++i) {
    const char c = line[i];
    if ((c >= '0' && c <= '9') || c == '.' || c == ',' || c == '-' ||
        c == '+') {
      values += c;
    } else {
      break;
    }
  }
  const auto parts = split_ws(values);
  if (parts.size() < 3) return false;
  oe = {to_double(parts[0], "OELevels R"),
        to_double(parts[1], "OELevels G"),
        to_double(parts[2], "OELevels B")};
  return true;
}

// Minimal CGATS RGB reader for RawDigger _SG.txt: pulls SAMPLE_NAME + RGB_R/G/B
// by column index from the DATA_FORMAT block. Robust to the filename mismatch
// that read_rawdigger_patch_table validates against.
CgatsRgb read_cgats_rgb(
    const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("spectral closure: cannot open RGB " +
                                    path.string());
  std::vector<std::string> lines;
  for (std::string l; std::getline(is, l);) lines.push_back(l);

  std::vector<std::string> fmt;
  bool in_fmt = false, in_data = false;
  CgatsRgb out;
  int i_name = -1, i_r = -1, i_g = -1, i_b = -1, i_id = -1;
  for (const auto& raw : lines) {
    std::string l = raw;
    if (!l.empty() && l.back() == '\r') l.pop_back();
    const std::string trimmed = l;
    if (!out.has_oe_levels && parse_oe_levels(trimmed, out.oe_levels)) {
      out.has_oe_levels = true;
    }
    if (trimmed.rfind("BEGIN_DATA_FORMAT", 0) == 0) { in_fmt = true; continue; }
    if (trimmed.rfind("END_DATA_FORMAT", 0) == 0) {
      in_fmt = false;
      for (int k = 0; k < static_cast<int>(fmt.size()); ++k) {
        if (fmt[k] == "SAMPLE_NAME") i_name = k;
        else if (fmt[k] == "SAMPLE_ID") i_id = k;
        else if (fmt[k] == "RGB_R") i_r = k;
        else if (fmt[k] == "RGB_G") i_g = k;
        else if (fmt[k] == "RGB_B") i_b = k;
      }
      continue;
    }
    if (in_fmt) {
      for (auto& tok : split_ws(trimmed)) fmt.push_back(tok);
      continue;
    }
    if (trimmed.rfind("BEGIN_DATA", 0) == 0) { in_data = true; continue; }
    if (trimmed.rfind("END_DATA", 0) == 0) { in_data = false; continue; }
    if (in_data) {
      const auto f = split_ws(trimmed);
      const int need = std::max({i_name, i_id, i_r, i_g, i_b});
      if (need < 0 || static_cast<int>(f.size()) <= need) continue;
      const int nk = i_name >= 0 ? i_name : i_id;
      out.rows[f[static_cast<std::size_t>(nk)]] = {
          to_double(f[static_cast<std::size_t>(i_r)], "RGB_R"),
          to_double(f[static_cast<std::size_t>(i_g)], "RGB_G"),
          to_double(f[static_cast<std::size_t>(i_b)], "RGB_B")};
    }
  }
  if (i_r < 0 || i_g < 0 || i_b < 0 || out.rows.empty())
    throw std::runtime_error("spectral closure: no RGB rows in " +
                             path.string());
  return out;
}

std::array<double, 3> subtract_rgb(const std::array<double, 3>& v,
                                   const std::array<double, 3>& dark) {
  return {v[0] - dark[0], v[1] - dark[1], v[2] - dark[2]};
}

bool any_nonpositive(const std::array<double, 3>& v) {
  return v[0] <= 0 || v[1] <= 0 || v[2] <= 0;
}

bool any_near_oe(const std::array<double, 3>& v, const CgatsRgb& table,
                 double fraction) {
  if (!table.has_oe_levels) return false;
  for (int c = 0; c < 3; ++c) {
    if (table.oe_levels[static_cast<std::size_t>(c)] > 0 &&
        v[static_cast<std::size_t>(c)] >=
            fraction * table.oe_levels[static_cast<std::size_t>(c)]) {
      return true;
    }
  }
  return false;
}

// Reads a file split into logical lines regardless of CR, LF, or CRLF endings
// (the 2016 SpectraShop SG export is CR-only, old-Mac style).
std::vector<std::string> read_lines_any_eol(const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) throw std::runtime_error("spectral closure: cannot open " +
                                    path.string());
  std::string all((std::istreambuf_iterator<char>(is)),
                  std::istreambuf_iterator<char>());
  std::vector<std::string> lines;
  std::string cur;
  for (std::size_t i = 0; i < all.size(); ++i) {
    const char c = all[i];
    if (c == '\n') { lines.push_back(cur); cur.clear(); }
    else if (c == '\r') {
      lines.push_back(cur); cur.clear();
      if (i + 1 < all.size() && all[i + 1] == '\n') ++i;  // CRLF
    } else cur += c;
  }
  if (!cur.empty()) lines.push_back(cur);
  return lines;
}

struct Reflectance {
  std::vector<int> nm;
  std::map<std::string, std::vector<double>> by_patch;
};

// CGATS with paired SPECTRAL_NM/SPECTRAL_DEC columns: each data row is a patch,
// carrying (wavelength, reflectance) pairs. SAMPLE_ID/SAMPLE_NAME is the patch id.
// This covers both SG-140 and CC-24 SpectraShop exports. It is not the canonical
// patch_id,380,390,... CSV consumed by spectral-smi.
Reflectance read_paired_reflectance_cgats(const std::filesystem::path& path) {
  const auto lines = read_lines_any_eol(path);
  std::vector<std::string> fmt;
  bool in_fmt = false, in_data = false;
  std::vector<int> nm_cols, dec_cols;
  int id_col = -1;
  Reflectance out;
  for (const auto& l : lines) {
    std::string s = l;
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (s.rfind("BEGIN_DATA_FORMAT", 0) == 0) { in_fmt = true; continue; }
    if (s.rfind("END_DATA_FORMAT", 0) == 0) {
      in_fmt = false;
      for (int k = 0; k < static_cast<int>(fmt.size()); ++k) {
        if (fmt[k] == "SPECTRAL_NM") nm_cols.push_back(k);
        else if (fmt[k] == "SPECTRAL_DEC") dec_cols.push_back(k);
        else if ((fmt[k] == "SAMPLE_NAME") ||
                 (fmt[k] == "SAMPLE_ID" && id_col < 0))
          id_col = k;
      }
      continue;
    }
    if (in_fmt) { for (auto& t : split_ws(l)) fmt.push_back(t); continue; }
    if (s.rfind("BEGIN_DATA", 0) == 0) { in_data = true; continue; }
    if (s.rfind("END_DATA", 0) == 0) { in_data = false; continue; }
    if (in_data) {
      const auto f = split_ws(l);
      if (id_col < 0 || nm_cols.empty() || nm_cols.size() != dec_cols.size())
        throw std::runtime_error(
            "spectral closure: bad paired spectral reflectance format");
      if (static_cast<int>(f.size()) <= dec_cols.back()) continue;
      std::vector<double> refl;
      refl.reserve(nm_cols.size());
      for (std::size_t k = 0; k < nm_cols.size(); ++k) {
        const int w = static_cast<int>(std::llround(
            to_double(f[static_cast<std::size_t>(nm_cols[k])], "reflectance nm")));
        if (out.by_patch.empty()) out.nm.push_back(w);
        else if (out.nm[k] != w)
          throw std::runtime_error(
              "spectral closure: reflectance wavelength axis differs by row");
        refl.push_back(
            to_double(f[static_cast<std::size_t>(dec_cols[k])], "reflectance"));
      }
      out.by_patch[f[static_cast<std::size_t>(id_col)]] = std::move(refl);
    }
  }
  if (out.by_patch.empty() || out.nm.empty())
    throw std::runtime_error(
        "spectral closure: no paired spectral reflectance patches in " +
        path.string());
  return out;
}

std::string arg_value(int argc, char** argv, int& i) {
  if (i + 1 >= argc)
    throw std::runtime_error(std::string("spectral closure: missing value for ") +
                             argv[i]);
  return argv[++i];
}

void write_channel(JsonWriter& w, const char* key,
                   const SpectralClosureChannel& c) {
  w.key(key);
  w.begin_object();
  w.key("relative_rms"); w.value(c.relative_rms);
  w.key("correlation"); w.value(c.correlation);
  w.key("scale_k_diagnostic"); w.value(c.scale_k_diagnostic);
  w.end_object();
}

void write_rgb_array(JsonWriter& w, const char* key,
                     const std::array<double, 3>& rgb) {
  w.key(key);
  w.begin_array();
  w.value(rgb[0]);
  w.value(rgb[1]);
  w.value(rgb[2]);
  w.end_array();
}

}  // namespace

int cmd_spectral_closure(int argc, char** argv) {
  std::string ssf_csv, illuminant, reflectance, target_rgb, white_rgb, dark_rgb,
      out_path;
  std::string camera_model, dataset_id, archive_subset;
  double white_gate = 0.05;
  double saturation_fraction = 0.98;
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ssf-csv") ssf_csv = arg_value(argc, argv, i);
    else if (a == "--illuminant") illuminant = arg_value(argc, argv, i);
    else if (a == "--reflectance") reflectance = arg_value(argc, argv, i);
    else if (a == "--target-rgb") target_rgb = arg_value(argc, argv, i);
    else if (a == "--white-rgb") white_rgb = arg_value(argc, argv, i);
    else if (a == "--dark-rgb") dark_rgb = arg_value(argc, argv, i);
    else if (a == "--camera-model") camera_model = arg_value(argc, argv, i);
    else if (a == "--dataset-id") dataset_id = arg_value(argc, argv, i);
    else if (a == "--archive-subset") archive_subset = arg_value(argc, argv, i);
    else if (a == "--white-gate-max-error")
      white_gate = std::stod(arg_value(argc, argv, i));
    else if (a == "--saturation-mean-fraction")
      saturation_fraction = std::stod(arg_value(argc, argv, i));
    else if (a == "--out") out_path = arg_value(argc, argv, i);
    else {
      std::cerr << "spectral closure: unknown argument " << a << "\n";
      return 2;
    }
  }
  if (ssf_csv.empty() || illuminant.empty() || reflectance.empty() ||
      target_rgb.empty() || white_rgb.empty() || camera_model.empty() ||
      dataset_id.empty() || archive_subset.empty()) {
    std::cerr << "Usage: camera_iq spectral-closure --ssf-csv F --illuminant F "
                 "--reflectance F --target-rgb F --white-rgb F --camera-model N "
                 "--dataset-id ID --archive-subset L [--dark-rgb F] "
                 "[--white-gate-max-error X] [--saturation-mean-fraction X] "
                 "[--out F]\n"
                 "Tier-3 physical closure: predict ColorChecker RGB from "
                 "SSF x illuminant x reflectance vs the measured target.\n";
    return 2;
  }

  try {
    const auto ssf = read_ssf_csv(ssf_csv);
    const auto spd = read_illuminant(illuminant);
    const auto refl = read_paired_reflectance_cgats(reflectance);
    const auto measured = read_cgats_rgb(target_rgb);
    const auto white_map = read_cgats_rgb(white_rgb);
    const bool subtract_dark = !dark_rgb.empty();
    CgatsRgb dark_map;
    if (subtract_dark) dark_map = read_cgats_rgb(dark_rgb);

    // Common 10 nm grid over the reflectance range (limiting factor) that is
    // also covered by the SSF and the illuminant span.
    std::vector<int> grid;
    for (int w : refl.nm) {
      if (w % 10 != 0) continue;
      if (ssf.find(w) == ssf.end()) continue;
      if (w < spd.front().first - 1e-6 || w > spd.back().first + 1e-6) continue;
      grid.push_back(w);
    }
    std::sort(grid.begin(), grid.end());
    grid.erase(std::unique(grid.begin(), grid.end()), grid.end());
    if (grid.size() < 2)
      throw std::runtime_error("spectral closure: empty common wavelength grid");

    SpectralClosureInputs in;
    in.grid_nm = grid;
    in.white_gate_max_ratio_error = white_gate;
    for (int w : grid) {
      const auto& s = ssf.at(w);
      in.ssf[0].push_back(s[0]);
      in.ssf[1].push_back(s[1]);
      in.ssf[2].push_back(s[2]);
      in.illuminant.push_back(interp(spd, static_cast<double>(w)));
    }

    // Map reflectance wavelength -> column index for per-patch resampling.
    std::map<int, std::size_t> refl_col;
    for (std::size_t k = 0; k < refl.nm.size(); ++k) refl_col[refl.nm[k]] = k;

    std::size_t matched = 0, missing = 0;
    std::size_t target_dark_subtracted = 0, target_missing_dark = 0;
    std::size_t target_below_dark = 0, target_saturated = 0;
    for (const auto& [pid, prefl] : refl.by_patch) {
      const auto it = measured.rows.find(pid);
      if (it == measured.rows.end()) { ++missing; continue; }
      ++matched;
      std::array<double, 3> corrected = it->second;
      // Saturation is a capture-ceiling property, so test the target sidecar
      // value before subtracting the dark residual. Comparing target-dark here
      // would under-exclude by the dark residual near the OELevels threshold.
      if (any_near_oe(it->second, measured, saturation_fraction)) {
        ++target_saturated;
        continue;
      }
      if (subtract_dark) {
        const auto dit = dark_map.rows.find(pid);
        if (dit == dark_map.rows.end()) { ++target_missing_dark; continue; }
        corrected = subtract_rgb(corrected, dit->second);
        ++target_dark_subtracted;
        if (any_nonpositive(corrected)) {
          ++target_below_dark;
          continue;
        }
      }
      std::vector<double> r;
      r.reserve(grid.size());
      for (int w : grid) r.push_back(prefl[refl_col.at(w)]);
      in.patch_ids.push_back(pid);
      in.reflectance.push_back(std::move(r));
      in.measured_rgb.push_back(corrected);
    }
    if (in.patch_ids.empty())
      throw std::runtime_error(
          "spectral closure: no reflectance/measured patch id matches");
    if (subtract_dark && target_missing_dark > 0)
      throw std::runtime_error(
          "spectral closure: target RGB rows missing matching dark rows");

    // White card RGB = mean over all sampled points (uniform card).
    std::array<double, 3> white{0, 0, 0};
    std::size_t white_dark_subtracted = 0, white_missing_dark = 0,
                white_below_dark = 0, white_saturated = 0, white_accepted = 0;
    for (const auto& [name, rgb] : white_map.rows) {
      if (any_near_oe(rgb, white_map, saturation_fraction)) ++white_saturated;
      std::array<double, 3> corrected = rgb;
      if (subtract_dark) {
        const auto dit = dark_map.rows.find(name);
        if (dit == dark_map.rows.end()) { ++white_missing_dark; continue; }
        corrected = subtract_rgb(corrected, dit->second);
        ++white_dark_subtracted;
        if (any_nonpositive(corrected)) {
          ++white_below_dark;
          continue;
        }
      }
      white[0] += corrected[0]; white[1] += corrected[1]; white[2] += corrected[2];
      ++white_accepted;
    }
    if (subtract_dark && white_missing_dark > 0)
      throw std::runtime_error(
          "spectral closure: white RGB rows missing matching dark rows");
    const double wn = static_cast<double>(white_accepted);
    if (wn <= 0 || white_saturated > 0 || white_below_dark > 0)
      throw std::runtime_error(
          "spectral closure: white RGB is saturated or not above dark");
    in.white_rgb = {white[0] / wn, white[1] / wn, white[2] / wn};

    const auto res = compute_spectral_closure(in);

    std::ostringstream os;
    JsonWriter w(os);
    w.begin_object();
    w.key("validation_tier"); w.value(res.validation_tier);
    w.key("camera_model"); w.value(camera_model);
    w.key("dataset_id"); w.value(dataset_id);
    w.key("archive_subset"); w.value(archive_subset);
    w.key("grid_nm");
    w.begin_array();
    for (int g : grid) w.value(static_cast<std::int64_t>(g));
    w.end_array();
    w.key("patch_count"); w.value(static_cast<std::int64_t>(res.patches.size()));
    w.key("matched_patches"); w.value(static_cast<std::int64_t>(matched));
    w.key("unmatched_patches"); w.value(static_cast<std::int64_t>(missing));
    w.key("extraction");
    w.begin_object();
    w.key("dark_rgb_subtracted"); w.value(subtract_dark);
    w.key("target_dark_subtracted_patch_count");
    w.value(static_cast<std::int64_t>(target_dark_subtracted));
    w.key("target_missing_dark_patch_count");
    w.value(static_cast<std::int64_t>(target_missing_dark));
    w.key("target_below_dark_excluded_patch_count");
    w.value(static_cast<std::int64_t>(target_below_dark));
    w.key("target_saturated_excluded_patch_count");
    w.value(static_cast<std::int64_t>(target_saturated));
    w.key("white_dark_subtracted_sample_count");
    w.value(static_cast<std::int64_t>(white_dark_subtracted));
    w.key("white_below_dark_sample_count");
    w.value(static_cast<std::int64_t>(white_below_dark));
    w.key("white_saturated_sample_count");
    w.value(static_cast<std::int64_t>(white_saturated));
    w.key("saturation_mean_fraction"); w.value(saturation_fraction);
    w.end_object();
    w.key("white_card_gate");
    w.begin_object();
    w.key("attempted"); w.value(res.white_card_gate_attempted);
    w.key("passes"); w.value(res.white_card_gate_passes);
    w.key("max_ratio_error"); w.value(res.white_card_max_ratio_error);
    w.key("measured_ratio_rg"); w.value(res.white_ratio_measured[0]);
    w.key("measured_ratio_bg"); w.value(res.white_ratio_measured[1]);
    w.key("predicted_ratio_rg"); w.value(res.white_ratio_predicted[0]);
    w.key("predicted_ratio_bg"); w.value(res.white_ratio_predicted[1]);
    w.end_object();
    w.key("global_scale_k"); w.value(res.global_scale_k);
    write_channel(w, "r", res.r);
    write_channel(w, "g", res.g);
    write_channel(w, "b", res.b);
    w.key("scale_mode"); w.value("global_single_k");
    w.key("chart_identity"); w.value("measured_same_session");
    w.key("conclusion"); w.value(res.conclusion);
    w.key("patches");
    w.begin_array();
    for (const auto& p : res.patches) {
      w.begin_object();
      w.key("id"); w.value(p.id);
      write_rgb_array(w, "measured_rgb", p.measured);
      write_rgb_array(w, "predicted_rgb", p.predicted);
      const std::array<double, 3> residual{
          p.measured[0] - p.predicted[0],
          p.measured[1] - p.predicted[1],
          p.measured[2] - p.predicted[2]};
      write_rgb_array(w, "residual_rgb", residual);
      w.end_object();
    }
    w.end_array();
    w.end_object();

    const std::string json = os.str();
    if (!out_path.empty()) {
      std::ofstream ofs(out_path, std::ios::binary);
      ofs << json << "\n";
    } else {
      std::cout << json << "\n";
    }
    std::cerr << "spectral closure: gate "
              << (res.white_card_gate_passes ? "PASS" : "FAIL")
              << ", k=" << res.global_scale_k
              << ", R/G/B rel-rms=" << res.r.relative_rms << "/"
              << res.g.relative_rms << "/" << res.b.relative_rms << "\n";
    return res.white_card_gate_passes ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "camera_iq spectral-closure: " << e.what() << "\n";
    return 1;
  }
}

}  // namespace camera_iq
