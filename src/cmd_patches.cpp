#include "camera_iq/commands.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/color_reference.hpp"
#include "camera_iq/dataset_config.hpp"
#include "camera_iq/demosaic.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/patches.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {
namespace {

struct Args {
  std::filesystem::path raw_file;
  std::string dataset_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path coords;
  std::filesystem::path rawdigger_csv;
  std::filesystem::path reference_rgb;
  std::filesystem::path out;
};

struct ResolvedPath {
  std::filesystem::path actual;
  std::string label;
};

void usage() {
  std::cerr << "Usage: camera_iq patches <raw-file>"
               " (--coords FILE | --rawdigger-csv FILE)"
               " [--dataset ID] [--config FILE]"
               " [--reference-rgb FILE] [--out FILE]\n"
               "--coords reads checker2colors-style x,y,width,height rows;\n"
               "--rawdigger-csv reads RAW-space RawDigger patch exports.\n";
}

ResolvedPath resolve_dataset_side_path(const ResolvedDataset& dataset,
                                       const std::filesystem::path& path) {
  if (path.is_absolute()) {
    return {path, path.string()};
  }
  const auto inside_dataset = dataset.root / path;
  if (std::filesystem::exists(inside_dataset)) {
    return {inside_dataset, dataset_file_label(dataset.id, path)};
  }
  return {path, path.generic_string()};
}

void write_rgb(JsonWriter& w, const CameraRgbPatch& rgb) {
  w.begin_object();
  w.key("r");
  w.value(rgb.r);
  w.key("g");
  w.value(rgb.g);
  w.key("b");
  w.value(rgb.b);
  w.end_object();
}

void write_coord(JsonWriter& w, const PatchCoord& coord) {
  w.begin_object();
  w.key("x");
  w.value(coord.x);
  w.key("y");
  w.value(coord.y);
  w.key("width");
  w.value(coord.width);
  w.key("height");
  w.value(coord.height);
  w.end_object();
}

void write_patch(JsonWriter& w, const PatchMean& patch, std::size_t index,
                 const std::vector<std::string>& sample_names) {
  w.begin_object();
  w.key("index");
  w.value(static_cast<std::int64_t>(index));
  if (index < sample_names.size()) {
    w.key("sample_name");
    w.value(sample_names[index]);
  }
  w.key("source_coord");
  write_coord(w, patch.source_coord);
  w.key("actual_roi");
  w.begin_object();
  w.key("x");
  w.value(patch.x);
  w.key("y");
  w.value(patch.y);
  w.key("width");
  w.value(patch.width);
  w.key("height");
  w.value(patch.height);
  w.end_object();
  w.key("sample_count");
  w.value(static_cast<std::int64_t>(patch.sample_count));
  w.key("rgb_mean");
  write_rgb(w, patch.rgb);
  w.end_object();
}

void write_comparison(JsonWriter& w, const PatchComparison& comparison,
                      const std::string& reference_label) {
  w.begin_object();
  w.key("reference_rgb_path");
  w.value(reference_label);
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(comparison.patch_count));
  w.key("method");
  w.value("per_channel_pearson_and_affine_fit");
  w.key("channels");
  w.begin_array();
  for (const auto& c : comparison.channels) {
    w.begin_object();
    w.key("channel");
    w.value(c.channel);
    w.key("correlation");
    w.value(c.correlation);
    w.key("slope");
    w.value(c.slope);
    w.key("intercept");
    w.value(c.intercept);
    w.key("rmse_after_affine");
    w.value(c.rmse_after_affine);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_report(std::ostream& os, const std::string& file_label,
                  const std::string& coords_label, const RawCfaImage& cfa,
                  const std::vector<PatchMean>& patches,
                  const std::vector<std::string>& sample_names,
                  const std::optional<PatchComparison>& comparison,
                  const std::string& reference_label) {
  JsonWriter w(os);
  w.begin_object();
  w.key("file");
  w.value(file_label);
  w.key("coords_path");
  w.value(coords_label);
  w.key("coordinate_origin");
  w.value("matlab_checker2colors_one_based_top_left");
  w.key("rgb_source");
  w.value("bilinear_demosaic_black_subtracted_raw");
  w.key("camera");
  w.begin_object();
  w.key("make");
  w.value(cfa.meta.make);
  w.key("model");
  w.value(cfa.meta.model);
  w.key("cfa_pattern");
  w.value(cfa.meta.cfa_pattern);
  w.key("black_level");
  w.value(cfa.meta.black_level);
  w.key("white_level");
  w.value(cfa.meta.white_level);
  w.end_object();
  w.key("image");
  w.begin_object();
  w.key("width");
  w.value(cfa.width);
  w.key("height");
  w.value(cfa.height);
  w.key("algorithm");
  w.value("bilinear");
  w.end_object();
  w.key("patch_count");
  w.value(static_cast<std::int64_t>(patches.size()));
  w.key("patches");
  w.begin_array();
  for (std::size_t i = 0; i < patches.size(); ++i) {
    write_patch(w, patches[i], i, sample_names);
  }
  w.end_array();
  w.key("comparison");
  if (comparison) {
    write_comparison(w, *comparison, reference_label);
  } else {
    w.null();
  }
  w.key("limitations");
  w.begin_array();
  w.value("Bilinear demosaic only; not LibRaw/AHD or production ISP color");
  w.value("No sphere flat-field correction in this command yet");
  w.value("Reference RGB comparison is affine/correlation validation, not DeltaE");
  w.end_array();
  w.end_object();
}

}  // namespace

int cmd_patches(int argc, char** argv) {
  Args args;
  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--dataset") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --dataset requires an id\n";
        return 2;
      }
      args.dataset_id = argv[i];
    } else if (arg == "--config") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --config requires a path\n";
        return 2;
      }
      args.config = argv[i];
    } else if (arg == "--coords") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --coords requires a path\n";
        return 2;
      }
      args.coords = argv[i];
    } else if (arg == "--rawdigger-csv") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --rawdigger-csv requires a path\n";
        return 2;
      }
      args.rawdigger_csv = argv[i];
    } else if (arg == "--reference-rgb") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --reference-rgb requires a path\n";
        return 2;
      }
      args.reference_rgb = argv[i];
    } else if (arg == "--out") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --out requires a path\n";
        return 2;
      }
      args.out = argv[i];
    } else if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    } else if (args.raw_file.empty()) {
      args.raw_file = arg;
    } else {
      std::cerr << "camera_iq patches: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }

  if (args.raw_file.empty() ||
      (args.coords.empty() && args.rawdigger_csv.empty())) {
    usage();
    return 2;
  }
  if (!args.coords.empty() && !args.rawdigger_csv.empty()) {
    std::cerr << "camera_iq patches: use either --coords or --rawdigger-csv,"
                 " not both\n";
    return 2;
  }

  try {
    std::filesystem::path actual_raw = args.raw_file;
    std::string file_label = args.raw_file.string();
    std::filesystem::path actual_coords = args.coords;
    std::string coords_label = args.coords.string();
    std::filesystem::path actual_rawdigger_csv;
    std::string rawdigger_label;
    std::filesystem::path actual_reference_rgb;
    std::string reference_label;

    std::optional<ResolvedDataset> dataset;
    if (!args.dataset_id.empty()) {
      dataset = resolve_dataset_root(args.dataset_id, args.config);
      if (!dataset || !dataset->from_config) {
        std::cerr << "camera_iq patches: dataset id '" << args.dataset_id
                  << "' not found in " << args.config << "\n";
        return 1;
      }
      if (args.raw_file.is_absolute()) {
        std::cerr << "camera_iq patches: --dataset requires a relative raw file\n";
        return 2;
      }
      actual_raw = dataset->root / args.raw_file;
      file_label = dataset_file_label(args.dataset_id, args.raw_file);
      if (!args.coords.empty()) {
        const auto resolved_coords =
            resolve_dataset_side_path(*dataset, args.coords);
        actual_coords = resolved_coords.actual;
        coords_label = resolved_coords.label;
      }
      if (!args.rawdigger_csv.empty()) {
        const auto resolved_rawdigger =
            resolve_dataset_side_path(*dataset, args.rawdigger_csv);
        actual_rawdigger_csv = resolved_rawdigger.actual;
        rawdigger_label = resolved_rawdigger.label;
        coords_label = rawdigger_label;
      }

      if (args.reference_rgb.empty() && args.rawdigger_csv.empty()) {
        const auto configs = read_dataset_config(args.config);
        const auto it = configs.find(args.dataset_id);
        if (it != configs.end() && it->second.color_reference &&
            !it->second.color_reference->pairing_rgb_path.empty()) {
          args.reference_rgb = it->second.color_reference->pairing_rgb_path;
        }
      }
    }

    if (!args.rawdigger_csv.empty() && !dataset) {
      actual_rawdigger_csv = args.rawdigger_csv;
      rawdigger_label = args.rawdigger_csv.string();
      coords_label = rawdigger_label;
    }

    if (!args.reference_rgb.empty()) {
      if (dataset) {
        const auto resolved_ref =
            resolve_dataset_side_path(*dataset, args.reference_rgb);
        actual_reference_rgb = resolved_ref.actual;
        reference_label = resolved_ref.label;
      } else {
        actual_reference_rgb = args.reference_rgb;
        reference_label = args.reference_rgb.string();
      }
    }

    const auto cfa = read_raw_cfa_image(actual_raw);
    if (!cfa) {
      std::cerr << "camera_iq patches: cannot read/unpack " << file_label
                << "\n";
      return 1;
    }
    const auto rgb = demosaic_bilinear(cfa->samples.data(), cfa->width,
                                       cfa->height, cfa->row_stride_pixels,
                                       cfa->color_at_position, cfa->cdesc);
    if (rgb.empty()) {
      std::cerr << "camera_iq patches: unsupported CFA descriptor for "
                << file_label << "\n";
      return 1;
    }
    std::vector<PatchCoord> coords;
    std::vector<std::string> sample_names;
    std::optional<std::vector<CameraRgbPatch>> embedded_reference_rgb;
    if (!actual_rawdigger_csv.empty()) {
      const auto rawdigger = read_rawdigger_patch_table(
          actual_rawdigger_csv, actual_raw.filename().string());
      coords = rawdigger.coords;
      sample_names = rawdigger.sample_names;
      if (actual_reference_rgb.empty()) {
        embedded_reference_rgb = rawdigger.reference_rgb;
        reference_label = rawdigger_label + "#Ravg/Gavg/Bavg";
      }
    } else {
      coords = read_patch_coords_csv(actual_coords);
    }
    const auto patches = extract_patch_means(rgb, cfa->width, cfa->height,
                                             coords);

    std::optional<PatchComparison> comparison;
    if (!actual_reference_rgb.empty()) {
      comparison = compare_patch_means_to_rgb(
          patches, read_camera_rgb_csv(actual_reference_rgb));
    } else if (embedded_reference_rgb) {
      comparison = compare_patch_means_to_rgb(patches, *embedded_reference_rgb);
    }

    if (args.out.empty()) {
      write_report(std::cout, file_label, coords_label, *cfa, patches,
                   sample_names, comparison, reference_label);
      std::cout << "\n";
    } else {
      std::ofstream os(args.out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq patches: cannot write " << args.out << "\n";
        return 1;
      }
      write_report(os, file_label, coords_label, *cfa, patches, sample_names,
                   comparison, reference_label);
      os << "\n";
      std::cerr << "patch means written to " << args.out << "\n";
    }
  } catch (const std::exception& ex) {
    std::cerr << "camera_iq patches: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}

}  // namespace camera_iq
