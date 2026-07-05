#include "camera_iq/toolkit.hpp"

#include <iostream>
#include <string>
#include <string_view>

#include "camera_iq/commands.hpp"

#ifndef CAMERA_IQ_VERSION
#define CAMERA_IQ_VERSION "0.0.0"
#endif

namespace camera_iq {

std::string version() { return CAMERA_IQ_VERSION; }

namespace {

void print_usage() {
  std::cout <<
      "camera_iq " << version() << "\n"
      "Camera image-quality analysis toolkit.\n\n"
      "Usage:\n"
      "  camera_iq <command> [options]\n\n"
      "Commands:\n"
      "  manifest    Enumerate a dataset folder and emit a JSON manifest\n"
      "  raw-stats   Per-CFA-channel statistics for a raw capture\n"
      "  demosaic    Bilinear demosaic summary for a raw capture\n"
      "  dark-calibration\n"
      "              Reconcile metadata black against measured dark frames\n"
      "  exposure-response\n"
      "              Black-subtracted CFA summaries grouped by exposure series\n"
      "  oecf-fit\n"
      "              Relative-exposure linearity fit over usable OECF points\n"
      "  reference-info\n"
      "              Inspect configured ColorChecker spectral reference metadata\n"
      "\n"
      "Commands (planned):\n"
      "  patches     Extract ColorChecker patch statistics\n"
      "  ccm         Fit and apply a color-correction matrix, report deltaE\n"
      "  noise       Read noise / DSNU / PRNU summaries\n\n"
      "Options:\n"
      "  -h, --help       Show this help\n"
      "  -v, --version    Show version\n";
}

}  // namespace

int run(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 0;
  }

  const std::string_view arg = argv[1];
  if (arg == "-h" || arg == "--help") {
    print_usage();
    return 0;
  }
  if (arg == "-v" || arg == "--version") {
    std::cout << version() << "\n";
    return 0;
  }
  if (arg == "manifest") {
    return cmd_manifest(argc - 2, argv + 2);
  }
  if (arg == "raw-stats") {
    return cmd_raw_stats(argc - 2, argv + 2);
  }
  if (arg == "demosaic") {
    return cmd_demosaic(argc - 2, argv + 2);
  }
  if (arg == "dark-calibration") {
    return cmd_dark_calibration(argc - 2, argv + 2);
  }
  if (arg == "exposure-response") {
    return cmd_exposure_response(argc - 2, argv + 2);
  }
  if (arg == "oecf-fit") {
    return cmd_oecf_fit(argc - 2, argv + 2);
  }
  if (arg == "reference-info") {
    return cmd_reference_info(argc - 2, argv + 2);
  }

  std::cerr << "camera_iq: command '" << arg
            << "' is not implemented yet.\n"
               "Run 'camera_iq --help' for the planned command list.\n";
  return 2;
}

}  // namespace camera_iq
