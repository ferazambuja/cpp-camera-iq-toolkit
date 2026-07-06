#include "camera_iq/spectral_response.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "harness.hpp"

namespace fs = std::filesystem;

using camera_iq::SpectralResponseProvenance;
using camera_iq::parse_spectral_response;
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
}
