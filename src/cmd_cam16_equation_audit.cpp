#include "camera_iq/commands.hpp"

#include <filesystem>
#include <iostream>
#include <string_view>

#include "camera_iq/cam16_equation_audit.hpp"
#include "camera_iq/output_file.hpp"

namespace camera_iq {
namespace {

void print_usage(std::ostream& os) {
  os << "Usage: camera_iq cam16-equation-audit [options]\n"
        "\n"
        "Reproduce equation-level diagnostics from Hellwig and Fairchild\n"
        "(2022). This is not observer validation or a full CAM16 implementation.\n"
        "\n"
        "Options:\n"
        "  --out-json FILE  Write structured audit JSON\n"
        "  --out-csv FILE   Write deterministic curve CSV\n"
        "  -h, --help       Show this help\n";
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

}  // namespace

int cmd_cam16_equation_audit(int argc, char** argv) {
  std::filesystem::path out_json;
  std::filesystem::path out_csv;
  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      print_usage(std::cout);
      return 0;
    }
    if (arg == "--out-json" || arg == "--out-csv") {
      if (i + 1 >= argc) {
        std::cerr << "camera_iq cam16-equation-audit: " << arg
                  << " requires a value\n";
        return 2;
      }
      if (arg == "--out-json") {
        out_json = argv[++i];
      } else {
        out_csv = argv[++i];
      }
      continue;
    }
    std::cerr << "camera_iq cam16-equation-audit: unknown option '" << arg
              << "'\n";
    return 2;
  }
  if (same_path(out_json, out_csv)) {
    std::cerr
        << "camera_iq cam16-equation-audit: JSON and CSV outputs must differ\n";
    return 2;
  }

  const auto report = build_cam16_equation_audit();
  if (out_json.empty()) {
    write_cam16_equation_audit_json(std::cout, report);
    std::cout << '\n';
  } else if (!write_output_file_checked(
                 out_json, "cam16-equation-audit",
                 [&](std::ostream& output) {
                   write_cam16_equation_audit_json(output, report);
                 },
                 std::cerr)) {
    return 1;
  }
  if (!out_csv.empty() &&
      !write_output_file_checked(
          out_csv, "cam16-equation-audit",
          [&](std::ostream& output) {
            write_cam16_equation_audit_csv(output, report);
          },
          std::cerr, false)) {
    return 1;
  }
  std::cerr << "cam16-equation-audit: reproduced 21 brightness points, 8 "
               "isolated background-factor points, 72 coupled "
               "chroma-expression points, and 3 published performance "
               "comparisons\n";
  return 0;
}

}  // namespace camera_iq
