#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "camera_iq/csv.hpp"
#include "camera_iq/gamut_mapping_report.hpp"
#include "camera_iq/output_file.hpp"
#include "camera_iq/sha256.hpp"

namespace camera_iq {
namespace {

void print_usage(std::ostream& os) {
  os << "Usage: camera_iq gamut-map <input.csv> [options]\n"
        "\n"
        "Input CSV schema: id,r,g,b with finite encoded components in [0,1].\n"
        "\n"
        "Options:\n"
        "  --source display-p3|srgb       Source encoding (default display-p3)\n"
        "  --destination srgb|display-p3  Destination encoding (default srgb)\n"
        "  --intent radial-clip|soft-knee|oklch-radial|css-local-minde\n"
        "                                  Mapping method (default radial-clip)\n"
        "  --knee FRACTION                 Soft protected-core knee (default 0.75)\n"
        "  --out-json FILE                 Write structured JSON report\n"
        "  --out-csv FILE                  Write per-sample CSV report\n"
        "  -h, --help                      Show this help\n";
}

std::optional<RgbColorSpace> parse_space(std::string_view text) {
  if (text == "srgb") return RgbColorSpace::Srgb;
  if (text == "display-p3") return RgbColorSpace::DisplayP3;
  return std::nullopt;
}

std::optional<GamutMapIntent> parse_intent(std::string_view text) {
  if (text == "radial-clip") return GamutMapIntent::BoundaryProjection;
  if (text == "soft-knee") return GamutMapIntent::SoftChromaCompression;
  if (text == "oklch-radial")
    return GamutMapIntent::OklchBoundaryProjection;
  if (text == "css-local-minde")
    return GamutMapIntent::CssColor4LocalMinde;
  return std::nullopt;
}

std::filesystem::path comparable_path(const std::filesystem::path& path) {
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) return canonical;
  const auto absolute = std::filesystem::absolute(path, error);
  return error ? path.lexically_normal() : absolute.lexically_normal();
}

bool same_path(const std::filesystem::path& first,
               const std::filesystem::path& second) {
  if (first.empty() || second.empty()) return false;
  std::error_code error;
  if (std::filesystem::equivalent(first, second, error) && !error) return true;
  return comparable_path(first) == comparable_path(second);
}

std::string read_file_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open input CSV " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>());
}

}  // namespace

int cmd_gamut_map(int argc, char** argv) {
  std::filesystem::path input_path;
  std::filesystem::path out_json;
  std::filesystem::path out_csv;
  GamutMapOptions options;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--source" || arg == "--destination" || arg == "--intent" ||
        arg == "--knee" || arg == "--out-json" || arg == "--out-csv") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq gamut-map: " << arg << " requires a value\n";
        return 2;
      }
      const std::string_view value = argv[++i];
      if (arg == "--source") {
        const auto parsed = parse_space(value);
        if (!parsed) {
          std::cerr << "camera_iq gamut-map: unknown source '" << value
                    << "'\n";
          return 2;
        }
        options.source = *parsed;
      } else if (arg == "--destination") {
        const auto parsed = parse_space(value);
        if (!parsed) {
          std::cerr << "camera_iq gamut-map: unknown destination '" << value
                    << "'\n";
          return 2;
        }
        options.destination = *parsed;
      } else if (arg == "--intent") {
        const auto parsed = parse_intent(value);
        if (!parsed) {
          std::cerr << "camera_iq gamut-map: unknown intent '" << value
                    << "'\n";
          return 2;
        }
        options.intent = *parsed;
      } else if (arg == "--knee") {
        const auto parsed = parse_double(value);
        if (!parsed || *parsed < 0.0 || *parsed >= 1.0) {
          std::cerr << "camera_iq gamut-map: --knee must be within [0,1)\n";
          return 2;
        }
        options.knee_fraction = *parsed;
      } else if (arg == "--out-json") {
        out_json = value;
      } else {
        out_csv = value;
      }
    } else if (!arg.empty() && arg.front() == '-') {
      std::cerr << "camera_iq gamut-map: unknown option '" << arg << "'\n";
      return 2;
    } else if (input_path.empty()) {
      input_path = arg;
    } else {
      std::cerr << "camera_iq gamut-map: unexpected argument '" << arg
                << "'\n";
      return 2;
    }
  }

  if (input_path.empty()) {
    print_usage(std::cerr);
    return 2;
  }
  if (same_path(input_path, out_json) || same_path(input_path, out_csv)) {
    std::cerr << "camera_iq gamut-map: output must not overwrite the input\n";
    return 2;
  }
  if (same_path(out_json, out_csv)) {
    std::cerr << "camera_iq gamut-map: JSON and CSV outputs must differ\n";
    return 2;
  }

  try {
    const std::string input_bytes = read_file_bytes(input_path);
    const auto samples = parse_gamut_samples_csv(input_bytes);
    const auto report = analyze_gamut_samples(
        samples, options, input_path.filename().string(),
        sha256_hex(input_bytes));

    if (out_json.empty()) {
      write_gamut_map_json(std::cout, report);
      std::cout << '\n';
    } else if (!write_output_file_checked(
                   out_json, "gamut-map",
                   [&](std::ostream& output) {
                     write_gamut_map_json(output, report);
                   },
                   std::cerr)) {
      return 1;
    }
    if (!out_csv.empty() &&
        !write_output_file_checked(
            out_csv, "gamut-map",
            [&](std::ostream& output) { write_gamut_map_csv(output, report); },
            std::cerr, false)) {
      return 1;
    }

    std::cerr << "gamut-map: " << report.samples.size() << " samples, "
              << report.out_of_gamut_count << " outside destination, "
              << report.modified_count
              << " modified; all accepted outputs passed the destination-gamut postcondition\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "camera_iq gamut-map: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace camera_iq
