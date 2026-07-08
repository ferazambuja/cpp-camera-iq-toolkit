#include "camera_iq/commands.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "camera_iq/dataset_config.hpp"
#include "camera_iq/filename_meta.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/raw_meta.hpp"
#include "camera_iq/roi.hpp"
#include "camera_iq/sfr.hpp"

namespace camera_iq {
namespace {

struct Args {
  std::string root_or_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path raw_rel;
  std::filesystem::path oracle_y_multi_rel;
  std::filesystem::path out = "out/sfr.json";
  std::optional<RoiRect> edge_roi;
  double near_saturation_fraction = 0.98;
};

void usage() {
  std::cerr
      << "Usage: camera_iq sfr <dataset-root-or-id> --raw REL "
         "(--edge-roi x,y,w,h | --oracle-y-multi REL) [--out FILE] "
         "[--config FILE] [--near-saturation-fraction F]\n"
      << "SFR: green-linear, single explicit ROI slanted-edge SFR/MTF. "
         "Imatest _Y_multi data is advisory only.\n";
}

bool require_value(int argc, int next_index, const std::string& option) {
  if (next_index < argc) return true;
  std::cerr << "camera_iq sfr: " << option << " requires a value\n";
  return false;
}

std::optional<double> parse_fraction(std::string_view text) {
  char* end = nullptr;
  const std::string copy{text};
  const double value = std::strtod(copy.c_str(), &end);
  if (end == copy.c_str() || *end != '\0' || value <= 0.0 || value > 1.0) {
    return std::nullopt;
  }
  return value;
}

std::filesystem::path resolve_child(const std::filesystem::path& root,
                                    const std::filesystem::path& child) {
  return child.is_absolute() ? child : (root / child);
}

void write_roi(JsonWriter& json, const RoiRect& roi) {
  json.begin_object();
  json.key("x");
  json.value(roi.x);
  json.key("y");
  json.value(roi.y);
  json.key("width");
  json.value(roi.width);
  json.key("height");
  json.value(roi.height);
  json.end_object();
}

void write_sfr_json(std::ostream& os, const ResolvedDataset& dataset,
                    const std::filesystem::path& raw_rel,
                    const RawMeta& meta,
                    const SfrResult& result,
                    const std::optional<ImatestYMultiOracle>& oracle) {
  JsonWriter json(os);
  json.begin_object();
  json.key("command");
  json.value("sfr");
  json.key("phase");
  json.value("A1_green_linear_center_roi");
  json.key("claim_scope");
  json.value("green_linear_single_roi_trend_gate_advisory_imatest");
  json.key("dataset");
  json.value(dataset.from_config ? dataset_root_label(dataset.id)
                                 : dataset.label);
  json.key("raw");
  json.value(dataset.from_config ? dataset_file_label(dataset.id, raw_rel)
                                 : raw_rel.generic_string());
  const std::string raw_filename = raw_rel.filename().string();
  const auto filename_meta = parse_capture_filename(raw_filename);
  json.key("provenance_checks");
  json.begin_object();
  json.key("raw_filename");
  json.value(raw_filename);
  json.key("oracle_filename_exact_match");
  if (oracle) {
    json.value(raw_filename == oracle->filename);
  } else {
    json.null();
  }
  json.key("filename_aperture");
  if (filename_meta.aperture) {
    json.value(*filename_meta.aperture);
  } else {
    json.null();
  }
  json.key("exif_aperture");
  if (meta.aperture > 0.0) {
    json.value(meta.aperture);
  } else {
    json.null();
  }
  json.key("filename_exif_aperture_match");
  if (filename_meta.aperture && meta.aperture > 0.0) {
    json.value(std::abs(*filename_meta.aperture - meta.aperture) < 0.02);
  } else {
    json.null();
  }
  json.end_object();
  json.key("channel");
  json.value(result.channel);
  json.key("gamma");
  json.value("none");
  json.key("demosaic");
  json.value("none");
  json.key("accepted");
  json.value(result.accepted);
  json.key("rejection_reason");
  if (result.rejection_reason.empty()) {
    json.null();
  } else {
    json.value(result.rejection_reason);
  }
  json.key("roi_active_area");
  write_roi(json, result.roi);
  json.key("orientation");
  json.value(result.orientation);
  json.key("green_sample_count");
  json.value(result.green_sample_count);
  json.key("saturated_fraction");
  json.value(result.saturated_fraction);
  json.key("contrast_dn");
  json.value(result.contrast_dn);
  json.key("edge_angle_deg");
  json.value(result.edge_angle_deg);
  json.key("oversample");
  json.value(result.oversample);
  json.key("mtf50_cy_per_px");
  json.value(result.mtf50_cy_per_px);
  json.key("mtf50p_cy_per_px");
  json.value(result.mtf50p_cy_per_px);
  json.key("mtf_at_nyquist_0_5_cy_per_px");
  json.value(result.mtf_at_nyquist);
  json.key("r1090_px");
  json.value(result.r1090_px);
  json.key("advisory_oracle");
  if (!oracle) {
    json.null();
  } else {
    json.begin_object();
    json.key("source");
    json.value("imatest_y_multi_10dec_single_batch");
    json.key("filename");
    json.value(oracle->filename);
    json.key("run_date");
    json.value(oracle->run_date);
    json.key("center_roi_full_frame");
    write_roi(json, oracle->center_roi_full_frame);
    json.key("center_mtf50_cy_per_px");
    json.value(oracle->center_mtf50_cy_per_px);
    json.key("center_mtf50p_cy_per_px");
    json.value(oracle->center_mtf50p_cy_per_px);
    json.key("mtf50_delta_cy_per_px");
    json.value(result.mtf50_cy_per_px - oracle->center_mtf50_cy_per_px);
    json.key("absolute_match_is_gate");
    json.value(false);
    json.end_object();
  }
  json.key("not_claimed");
  json.begin_array();
  json.value("absolute_match_to_imatest");
  json.value("luma_or_gamma_replicated_imatest_pipeline");
  json.value("lp_per_mm_without_pixel_pitch");
  json.end_array();
  json.end_object();
}

}  // namespace

int cmd_sfr(int argc, char** argv) {
  Args args;
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--raw") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.raw_rel = argv[++i];
    } else if (arg == "--edge-roi") {
      if (!require_value(argc, i + 1, arg)) return 2;
      const auto roi = parse_roi_spec(argv[++i]);
      if (!roi) {
        std::cerr << "camera_iq sfr: invalid --edge-roi\n";
        return 2;
      }
      args.edge_roi = *roi;
    } else if (arg == "--oracle-y-multi") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.oracle_y_multi_rel = argv[++i];
    } else if (arg == "--out") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.out = argv[++i];
    } else if (arg == "--config") {
      if (!require_value(argc, i + 1, arg)) return 2;
      args.config = argv[++i];
    } else if (arg == "--near-saturation-fraction") {
      if (!require_value(argc, i + 1, arg)) return 2;
      const auto parsed = parse_fraction(argv[++i]);
      if (!parsed) {
        std::cerr << "camera_iq sfr: invalid --near-saturation-fraction\n";
        return 2;
      }
      args.near_saturation_fraction = *parsed;
    } else if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (args.root_or_id.empty()) {
      args.root_or_id = arg;
    } else {
      std::cerr << "camera_iq sfr: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }

  if (args.root_or_id.empty() || args.raw_rel.empty()) {
    usage();
    return 2;
  }
  if (!args.edge_roi && args.oracle_y_multi_rel.empty()) {
    std::cerr << "camera_iq sfr: provide --edge-roi or --oracle-y-multi\n";
    return 2;
  }

  try {
    const auto dataset = resolve_dataset_root(args.root_or_id, args.config);
    if (!dataset) {
      std::cerr << "camera_iq sfr: '" << args.root_or_id
                << "' is not a directory or dataset id in " << args.config
                << "\n";
      return 1;
    }

    std::optional<ImatestYMultiOracle> oracle;
    if (!args.oracle_y_multi_rel.empty()) {
      const auto oracle_path =
          resolve_child(dataset->root, args.oracle_y_multi_rel);
      oracle = read_imatest_y_multi(oracle_path);
      if (!oracle) {
        std::cerr << "camera_iq sfr: cannot parse Imatest _Y_multi oracle "
                  << args.oracle_y_multi_rel << "\n";
        return 1;
      }
    }

    const auto raw_path = resolve_child(dataset->root, args.raw_rel);
    const auto image = read_raw_cfa_image(raw_path);
    if (!image) {
      std::cerr << "camera_iq sfr: cannot read/unpack RAW " << args.raw_rel
                << "\n";
      return 1;
    }

    RoiRect roi{};
    if (args.edge_roi) {
      roi = *args.edge_roi;
    } else {
      const auto active_roi =
          full_frame_roi_to_active_area(oracle->center_roi_full_frame,
                                        image->meta);
      if (!active_roi) {
        std::cerr << "camera_iq sfr: oracle ROI does not overlap active area\n";
        return 1;
      }
      roi = *active_roi;
    }

    SfrOptions options;
    options.near_saturation_fraction = args.near_saturation_fraction;
    const auto result = analyze_green_sfr(*image, roi, options);

    if (!args.out.parent_path().empty()) {
      std::filesystem::create_directories(args.out.parent_path());
    }
    std::ofstream os(args.out, std::ios::binary);
    if (!os) {
      std::cerr << "camera_iq sfr: cannot write " << args.out << "\n";
      return 1;
    }
    write_sfr_json(os, *dataset, args.raw_rel, image->meta, result, oracle);
  } catch (const std::exception& e) {
    std::cerr << "camera_iq sfr: " << e.what() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace camera_iq
