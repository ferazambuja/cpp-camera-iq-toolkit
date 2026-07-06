#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "camera_iq/spectral_response.hpp"

namespace camera_iq {
namespace {

struct Args {
  std::filesystem::path response_csv;
  std::filesystem::path spd_csv;
  std::filesystem::path out = "out/spectral_response.json";
  SpectralResponseProvenance provenance;
};

void usage() {
  std::cerr
      << "Usage: camera_iq spectral-response --response-csv FILE --spd-csv "
         "FILE --camera-model NAME --dataset-id ID --archive-subset LABEL "
         "[--out FILE]\n"
      << "Parses legacy monochromator response/SPD CSVs as "
         "legacy_fidelity_only evidence.\n";
}

bool require_value(int argc, int next_index, const std::string& option) {
  if (next_index < argc) return true;
  std::cerr << "camera_iq spectral-response: " << option
            << " requires a value\n";
  return false;
}

}  // namespace

int cmd_spectral_response(int argc, char** argv) {
  Args args;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--response-csv") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.response_csv = argv[++i];
    } else if (arg == "--spd-csv") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.spd_csv = argv[++i];
    } else if (arg == "--camera-model") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.provenance.camera_model = argv[++i];
    } else if (arg == "--dataset-id") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.provenance.dataset_id = argv[++i];
    } else if (arg == "--archive-subset") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.provenance.archive_subset = argv[++i];
    } else if (arg == "--out") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.out = argv[++i];
    } else if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else {
      std::cerr << "camera_iq spectral-response: unexpected argument '" << arg
                << "'\n";
      return 2;
    }
  }

  if (args.response_csv.empty() || args.spd_csv.empty() ||
      args.provenance.camera_model.empty() ||
      args.provenance.dataset_id.empty() ||
      args.provenance.archive_subset.empty()) {
    usage();
    return 2;
  }

  try {
    const auto response =
        parse_spectral_response(args.response_csv, args.spd_csv,
                                args.provenance);
    if (!args.out.parent_path().empty()) {
      std::filesystem::create_directories(args.out.parent_path());
    }
    std::ofstream os(args.out, std::ios::binary);
    if (!os) {
      std::cerr << "camera_iq spectral-response: cannot write " << args.out
                << "\n";
      return 1;
    }
    write_spectral_response_json(os, response);
  } catch (const std::exception& e) {
    std::cerr << "camera_iq spectral-response: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace camera_iq
