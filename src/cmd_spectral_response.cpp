#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <optional>
#include <string>

#include "camera_iq/output_file.hpp"
#include "camera_iq/roi.hpp"
#include "camera_iq/spectral_response.hpp"

namespace camera_iq {
namespace {

struct Args {
  std::filesystem::path response_csv;
  std::filesystem::path spd_csv;
  std::filesystem::path raw_dir;
  std::filesystem::path dark_raw;
  std::filesystem::path ssf_csv_out;
  std::filesystem::path out = "out/spectral_response.json";
  RoiRect roi;
  bool has_roi = false;
  double near_saturation_fraction = 0.98;
  SpectralResponseProvenance provenance;
};

void usage() {
  std::cerr
      << "Usage: camera_iq spectral-response --response-csv FILE --spd-csv "
         "FILE --camera-model NAME --dataset-id ID --archive-subset LABEL "
         "[--raw-dir DIR --dark-raw FILE] [--roi x,y,w,h] "
         "[--near-saturation-fraction F] [--out FILE]\n"
      << "Parses legacy monochromator response/SPD CSVs as "
         "legacy_fidelity_only evidence. With RAW inputs, also emits toolkit "
         "RAW extraction and tier-1 legacy-fidelity comparison.\n";
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
    } else if (arg == "--raw-dir") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.raw_dir = argv[++i];
    } else if (arg == "--ssf-csv-out") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.ssf_csv_out = argv[++i];
    } else if (arg == "--dark-raw") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.dark_raw = argv[++i];
    } else if (arg == "--roi") {
      if (!require_value(argc, i + 1, arg)) return 2;
      const auto roi = parse_roi_spec(argv[++i]);
      if (!roi) {
        std::cerr << "camera_iq spectral-response: invalid --roi\n";
        return 2;
      }
      args.roi = *roi;
      args.has_roi = true;
    } else if (arg == "--near-saturation-fraction") {
      if (!require_value(argc, i + 1, arg)) return 2;
      char* end = nullptr;
      args.near_saturation_fraction = std::strtod(argv[++i], &end);
      if (end == argv[i] || *end != '\0' ||
          args.near_saturation_fraction <= 0.0 ||
          args.near_saturation_fraction > 1.0) {
        std::cerr << "camera_iq spectral-response: invalid "
                     "--near-saturation-fraction\n";
        return 2;
      }
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
  if (args.raw_dir.empty() != args.dark_raw.empty()) {
    std::cerr << "camera_iq spectral-response: --raw-dir and --dark-raw must "
                 "be provided together\n";
    return 2;
  }

  try {
    const auto response =
        parse_spectral_response(args.response_csv, args.spd_csv,
                                args.provenance);
    std::optional<SpectralRawExtraction> raw_extraction;
    if (!args.raw_dir.empty()) {
      raw_extraction = extract_raw_spectral_response_from_files(
          response, args.raw_dir, args.dark_raw,
          args.has_roi ? args.roi : RoiRect{}, args.near_saturation_fraction);
    }
    if (!write_output_file_checked(
            args.out, "spectral-response",
            [&](std::ostream& os) {
              if (raw_extraction) {
                write_spectral_raw_extraction_json(os, response,
                                                   *raw_extraction);
              } else {
                write_spectral_response_json(os, response);
              }
            },
            std::cerr)) {
      return 1;
    }

    // Emit the TOOLKIT-extracted SSF (dark-subtracted, saturation-guarded,
    // CFA-direct) as a Wavelength,R,G,B CSV so the closure and quality slices
    // can consume our own extraction instead of the legacy legacy-Gold curve.
    if (!args.ssf_csv_out.empty()) {
      if (!raw_extraction) {
        std::cerr << "camera_iq spectral-response: --ssf-csv-out requires "
                     "--raw-dir (there is no toolkit SSF without extraction)\n";
        return 2;
      }
      const auto& r = raw_extraction->response;
      if (!write_output_file_checked(
              args.ssf_csv_out, "spectral-response",
              [&](std::ostream& ss) {
                ss << "Wavelength (nm),Red,Green,Blue\n";
                for (std::size_t i = 0; i < r.axis_nm.size(); ++i) {
                  ss << r.axis_nm[i] << "," << r.response_r[i] << ","
                     << r.response_g[i] << "," << r.response_b[i] << "\n";
                }
              },
              std::cerr, /*append_newline=*/false)) {
        return 1;
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "camera_iq spectral-response: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace camera_iq
