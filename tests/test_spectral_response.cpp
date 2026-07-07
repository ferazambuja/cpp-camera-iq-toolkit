#include "camera_iq/spectral_response.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::SpectralResponseProvenance;
using camera_iq::RawCfaImage;
using camera_iq::RoiRect;
using camera_iq::discover_spectral_sweep_files;
using camera_iq::extract_raw_spectral_response;
using camera_iq::parse_spectral_response;
using camera_iq::write_spectral_raw_extraction_json;
using camera_iq::write_spectral_response_json;
using test::check;
using test::check_near;

namespace {

void write_file(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream os(path, std::ios::binary);
  os << text;
}

std::string legacy_response_csv(int sample_count = 48, int gap_index = -1,
                                std::string bad_value = {}) {
  std::string text = "Wavelength (nm),Red,Green,Blue\n";
  for (int i = 0; i < sample_count; ++i) {
    int wavelength = 360 + i * 10;
    if (i == gap_index) wavelength += 5;
    const std::string red = (!bad_value.empty() && i == 4) ? bad_value
                                                           : "0.25";
    const std::string green = i == 20 ? "1.0" : "0.5";
    text += std::to_string(wavelength) + "," + red + "," + green + ",0.125\n";
  }
  return text;
}

std::string spd_csv(int sample_count = 48, int gap_index = -1,
                    std::string bad_value = {}) {
  std::string text = " - [21:08:18] New Scan,Voltage (V) - [21:08:18] New Scan\n";
  for (int i = 0; i < sample_count; ++i) {
    double wavelength = 359.993 + i * 10.0;
    if (i == gap_index) wavelength += 6.0;
    const std::string voltage = (!bad_value.empty() && i == 7) ? bad_value
                                                               : "0.00001";
    text += std::to_string(wavelength) + "," + voltage + "\n";
  }
  return text;
}

bool throws_parse(const fs::path& response_csv, const fs::path& line_spd_csv) {
  try {
    (void)parse_spectral_response(response_csv, line_spd_csv, {});
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

camera_iq::SpectralResponse synthetic_legacy_response() {
  camera_iq::SpectralResponse response;
  response.camera_model = "Canon EOS 5D Mark II";
  response.dataset_id = "spectral_sensitivity_2016_2017";
  response.archive_subset = "synthetic";
  response.source = "legacy_bobby_gold_csv";
  response.normalization = "legacy_peak_channel_normalized_green_1_no_rescale";
  response.validation_tier = "legacy_fidelity_only";
  for (int i = 0; i < 48; ++i) {
    const double g = static_cast<double>(i + 1) / 48.0;
    response.axis_nm.push_back(360 + i * 10);
    response.response_r.push_back(0.5 * g);
    response.response_g.push_back(g);
    response.response_b.push_back(0.25 * g);
    response.line_spd.push_back(2.0);
  }
  return response;
}

RawCfaImage synthetic_cfa_image(const std::array<double, 4>& residuals,
                                double white_level = 1000.0) {
  RawCfaImage image;
  image.meta.make = "Canon";
  image.meta.model = "EOS 5D Mark II";
  image.meta.cfa_pattern = "GBRG";
  image.meta.visible_width = 4;
  image.meta.visible_height = 4;
  image.meta.black_per_channel = {100.0, 100.0, 100.0, 100.0};
  image.meta.black_level = 100.0;
  image.meta.white_level = white_level;
  image.width = 4;
  image.height = 4;
  image.row_stride_pixels = 4;
  image.color_at_position = {1, 2, 0, 3};
  image.cdesc = "RGBG";
  image.samples.resize(16);
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      const int pos = (r & 1) * 2 + (c & 1);
      image.samples[static_cast<std::size_t>(r * 4 + c)] =
          residuals[static_cast<std::size_t>(pos)];
    }
  }
  return image;
}

std::vector<RawCfaImage> synthetic_sweep(
    const std::array<double, 4>& dark_residuals, bool clipped = false,
    bool black_at_signal = false) {
  std::vector<RawCfaImage> images;
  images.reserve(48);
  for (int i = 0; i < 48; ++i) {
    const double scale = static_cast<double>(i + 1);
    std::array<double, 4> residuals{
        dark_residuals[0] + scale * 2.0,        // G1
        dark_residuals[1] + 0.25 * scale * 2.0, // B
        dark_residuals[2] + 0.5 * scale * 2.0,  // R
        dark_residuals[3] + scale * 2.0};       // G2
    if (clipped && i == 10) {
      residuals[2] = 890.0;  // raw = residual + black = 990, near white.
    }
    if (black_at_signal && i == 4) {
      residuals[2] = dark_residuals[2];
    }
    images.push_back(synthetic_cfa_image(residuals));
  }
  return images;
}

std::vector<RawCfaImage> synthetic_sweep_with_one_clipped_r_pixel(
    const std::array<double, 4>& dark_residuals) {
  auto images = synthetic_sweep(dark_residuals);
  // R is CFA position 2 (odd row, even column). Clip one of the four R samples
  // so extraction can exclude it, keep the other three, and report a flag.
  images[10].samples[static_cast<std::size_t>(1 * 4 + 0)] = 890.0;
  return images;
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_spectral_response";
  fs::remove_all(root);

  const fs::path response = root / "response.csv";
  const fs::path spd = root / "spd.csv";
  write_file(response, legacy_response_csv());
  write_file(spd, spd_csv());

  SpectralResponseProvenance provenance;
  provenance.camera_model = "Canon EOS 5D Mark II";
  provenance.dataset_id = "spectral_sensitivity_2016_2017";
  provenance.archive_subset = "canon_5d2/2016_11_21_5D2_Monochromator_OK";

  const auto parsed = parse_spectral_response(response, spd, provenance);
  check(parsed.camera_model == "Canon EOS 5D Mark II",
        "spectral response: camera provenance retained");
  check(parsed.dataset_id == "spectral_sensitivity_2016_2017",
        "spectral response: dataset provenance retained");
  check(parsed.archive_subset ==
            "canon_5d2/2016_11_21_5D2_Monochromator_OK",
        "spectral response: archive subset provenance retained");
  check(parsed.axis_nm.size() == 48, "spectral response: sample count");
  check(parsed.axis_nm.front() == 360 && parsed.axis_nm.back() == 830,
        "spectral response: rounded axis endpoints");
  check(parsed.axis_nm[1] - parsed.axis_nm[0] == 10,
        "spectral response: rounded axis spacing");
  check_near(parsed.response_g[20], 1.0, 1e-12,
             "spectral response: green peak preserved");
  check(parsed.normalization ==
            "legacy_peak_channel_normalized_green_1_no_rescale",
        "spectral response: legacy normalization labeled");
  check(parsed.validation_tier == "legacy_fidelity_only",
        "spectral response: legacy values are fidelity-only");

  std::ostringstream os;
  write_spectral_response_json(os, parsed);
  const std::string json = os.str();
  check(json.find("\"validation_tier\":\"legacy_fidelity_only\"") !=
            std::string::npos,
        "spectral response json: validation tier emitted");
  check(json.find("\"normalization\":\"legacy_peak_channel_normalized_green_1_no_rescale\"") !=
            std::string::npos,
        "spectral response json: normalization emitted");
  check(json.find("\"axis_nm\":[360,370") != std::string::npos,
        "spectral response json: axis emitted");

  write_file(root / "short_response.csv", legacy_response_csv(47));
  check(throws_parse(root / "short_response.csv", spd),
        "spectral response: 47 response samples rejected");

  write_file(root / "short_spd.csv", spd_csv(47));
  check(throws_parse(response, root / "short_spd.csv"),
        "spectral response: 47 SPD samples rejected");

  write_file(root / "gap_response.csv", legacy_response_csv(48, 2));
  check(throws_parse(root / "gap_response.csv", spd),
        "spectral response: 5nm response-axis gap rejected");

  write_file(root / "gap_spd.csv", spd_csv(48, 3));
  check(throws_parse(response, root / "gap_spd.csv"),
        "spectral response: misaligned SPD axis rejected");

  write_file(root / "nan_response.csv", legacy_response_csv(48, -1, "nan"));
  check(throws_parse(root / "nan_response.csv", spd),
        "spectral response: non-finite response rejected");

  write_file(root / "zero_spd.csv", spd_csv(48, -1, "0"));
  check(throws_parse(response, root / "zero_spd.csv"),
        "spectral response: zero SPD voltage rejected");

  write_file(root / "negative_spd.csv", spd_csv(48, -1, "-0.5"));
  check(throws_parse(response, root / "negative_spd.csv"),
        "spectral response: negative SPD voltage rejected");

  fs::path source_root = fs::current_path();
  if (!fs::exists(source_root / "data")) {
    source_root = source_root.parent_path();
  }
  const fs::path real_root = source_root /
      "data/private/datasets/spectral_sensitivity_2016_2017/canon_5d2/"
      "2016_11_21_5D2_Monochromator_OK";
  const fs::path real_response = real_root / "2016_11_21_5D2_mono.csv";
  const fs::path real_spd = real_root / "spd.csv";
  if (fs::exists(real_response) && fs::exists(real_spd)) {
    const auto real = parse_spectral_response(real_response, real_spd, provenance);
    check(real.axis_nm.size() == 48,
          "spectral response real validation: sample count");
    check(real.axis_nm.front() == 360 && real.axis_nm.back() == 830,
          "spectral response real validation: rounded axis");
    double max_g = 0;
    for (const double g : real.response_g) max_g = std::max(max_g, g);
    check_near(max_g, 1.0, 1e-12,
               "spectral response real validation: legacy G peak is 1");
  } else {
    check(true,
          "spectral response real validation: private Canon subset absent, skipped");
  }

  const auto legacy = synthetic_legacy_response();
  const std::array<double, 4> dark_residuals{1.0, 2.0, 3.0, 4.0};
  const auto dark = synthetic_cfa_image(dark_residuals);
  const auto extraction = extract_raw_spectral_response(
      legacy, synthetic_sweep(dark_residuals), dark, RoiRect{0, 0, 4, 4});
  check(extraction.response.source == "toolkit_raw_extraction",
        "raw spectral response: source labels toolkit extraction");
  check(extraction.response.validation_tier == "legacy_fidelity_only",
        "raw spectral response: tier remains fidelity-only");
  check(extraction.response.axis_nm.size() == 48,
        "raw spectral response: extracted sample count");
  check_near(extraction.response.response_g.back(), 1.0, 1e-12,
             "raw spectral response: green peak normalized to 1");
  check_near(extraction.response.response_r.back(), 0.5, 1e-12,
             "raw spectral response: red scaled by same green peak");
  check_near(extraction.response.response_b.back(), 0.25, 1e-12,
             "raw spectral response: blue scaled by same green peak");
  check_near(extraction.dark_residual_mean_by_position[0], 1.0, 1e-12,
             "raw spectral response: dark residual G1 measured");
  check_near(extraction.dark_residual_mean_by_position[2], 3.0, 1e-12,
             "raw spectral response: dark residual R measured");
  check_near(extraction.tier1_legacy_fidelity.r.rms, 0.0, 1e-12,
             "raw spectral response: red fidelity RMS");
  check_near(extraction.tier1_legacy_fidelity.g.correlation, 1.0, 1e-12,
             "raw spectral response: green fidelity correlation");

  std::vector<fs::path> private_paths;
  private_paths.reserve(48);
  const std::string private_prefix =
      std::string("/") + "Users/example/private/raw_";
  for (int i = 0; i < 48; ++i) {
    std::ostringstream frame;
    frame << std::setw(4) << std::setfill('0') << (592 + i);
    private_paths.push_back(private_prefix + frame.str() + ".CR2");
  }
  const auto private_path_extraction = extract_raw_spectral_response(
      legacy, synthetic_sweep(dark_residuals), dark, RoiRect{0, 0, 4, 4}, 0.98,
      private_paths);
  std::ostringstream raw_json;
  write_spectral_raw_extraction_json(raw_json, legacy, private_path_extraction);
  check(raw_json.str().find(private_prefix) == std::string::npos,
        "raw spectral response JSON: private directories are suppressed");
  check(raw_json.str().find("raw_0592.CR2") != std::string::npos,
        "raw spectral response JSON: raw filename is retained");

  bool threw = false;
  try {
    auto short_sweep = synthetic_sweep(dark_residuals);
    short_sweep.pop_back();
    (void)extract_raw_spectral_response(legacy, short_sweep, dark,
                                        RoiRect{0, 0, 4, 4});
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "raw spectral response: sweep-count mismatch rejected");

  threw = false;
  try {
    (void)extract_raw_spectral_response(
        legacy, synthetic_sweep(dark_residuals, true), dark,
        RoiRect{0, 0, 4, 4});
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "raw spectral response: clipped channel rejected");

  const auto partial_clip = extract_raw_spectral_response(
      legacy, synthetic_sweep_with_one_clipped_r_pixel(dark_residuals), dark,
      RoiRect{0, 0, 4, 4});
  check_near(partial_clip.samples[10].saturated_fraction_r, 0.25, 1e-12,
             "raw spectral response: partial saturation is flagged");
  check_near(partial_clip.response.response_g.back(), 1.0, 1e-12,
             "raw spectral response: partial saturation keeps extraction");

  const auto below_dark = extract_raw_spectral_response(
      legacy, synthetic_sweep(dark_residuals, false, true), dark,
      RoiRect{0, 0, 4, 4});
  check_near(below_dark.samples[4].below_dark_fraction_r, 1.0, 1e-12,
             "raw spectral response: below-dark red sample is flagged");
  check_near(below_dark.samples[4].mean_signal_r, 0.0, 1e-12,
             "raw spectral response: below-dark signal is clamped to zero");

  const fs::path map_root = root / "raw_map";
  fs::create_directories(map_root);
  for (int i = 0; i < 48; ++i) {
    std::ostringstream frame;
    frame << std::setw(4) << std::setfill('0') << (592 + i);
    write_file(map_root /
                   ("2016_11_21_5D2_mono_" + frame.str() + ".CR2"),
               "x");
  }
  const auto mapped = discover_spectral_sweep_files(map_root, legacy.axis_nm);
  check(mapped.size() == 48, "raw spectral response: filename map count");
  check(mapped.front().filename() == "2016_11_21_5D2_mono_0592.CR2" &&
            mapped.back().filename() == "2016_11_21_5D2_mono_0639.CR2",
        "raw spectral response: filename map is contiguous");

  fs::remove(map_root / "2016_11_21_5D2_mono_0601.CR2");
  threw = false;
  try {
    (void)discover_spectral_sweep_files(map_root, legacy.axis_nm);
  } catch (const std::runtime_error&) {
    threw = true;
  }
  check(threw, "raw spectral response: broken filename mapping rejected");
}
