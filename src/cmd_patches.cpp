#include "camera_iq/commands.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
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

constexpr double kFlatNearCeilingFraction = 0.98;
constexpr double kMaxFlatNearCeilingFraction = 0.01;

struct Args {
  std::filesystem::path raw_file;
  std::string dataset_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path coords;
  std::filesystem::path rawdigger_csv;
  std::filesystem::path reference_rgb;
  std::filesystem::path flat_field_raw;
  double flat_field_floor = 1.0;
  std::optional<WhiteBalanceGains> wb_gains;
  bool wb_from_flat_field = false;
  std::filesystem::path rgb_csv_out;
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
               " [--reference-rgb FILE]"
               " [--flat-field-raw FILE] [--flat-field-floor DN]"
               " [--wb-gains R,G,B | --wb-from-flat-field]"
               " [--rgb-csv-out FILE] [--out FILE]\n"
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

WhiteBalanceGains parse_wb_gains(const std::string& text) {
  std::stringstream ss(text);
  std::string part;
  std::vector<double> values;
  while (std::getline(ss, part, ',')) {
    try {
      std::size_t consumed = 0;
      const double value = std::stod(part, &consumed);
      if (consumed != part.size() || !std::isfinite(value) || value <= 0) {
        throw std::runtime_error("");
      }
      values.push_back(value);
    } catch (...) {
      throw std::runtime_error(
          "white balance gains must be three positive numbers: R,G,B");
    }
  }
  if (values.size() != 3) {
    throw std::runtime_error(
        "white balance gains must be three positive numbers: R,G,B");
  }
  return {values[0], values[1], values[2]};
}

CameraRgbPatch residual_ceiling_by_rgb_channel(const RawMeta& meta) {
  CameraRgbPatch sum;
  CameraRgbPatch count;
  for (std::size_t i = 0; i < meta.cfa_pattern.size() && i < 4; ++i) {
    const double ceiling = meta.white_level - meta.black_per_channel[i];
    if (!std::isfinite(ceiling) || ceiling <= 0) {
      throw std::runtime_error("flat field: invalid white/black metadata");
    }
    const char channel = meta.cfa_pattern[i];
    if (channel == 'R') {
      sum.r += ceiling;
      count.r += 1;
    } else if (channel == 'G') {
      sum.g += ceiling;
      count.g += 1;
    } else if (channel == 'B') {
      sum.b += ceiling;
      count.b += 1;
    }
  }
  if (count.r <= 0 || count.g <= 0 || count.b <= 0) {
    throw std::runtime_error("flat field: unsupported CFA channel layout");
  }
  return {sum.r / count.r, sum.g / count.g, sum.b / count.b};
}

std::size_t count_flat_samples_near_ceiling(
    const std::vector<RgbPixel>& flat, const CameraRgbPatch& ceiling,
    double fraction) {
  std::size_t count = 0;
  for (const auto& p : flat) {
    if (p.r >= ceiling.r * fraction) ++count;
    if (p.g >= ceiling.g * fraction) ++count;
    if (p.b >= ceiling.b * fraction) ++count;
  }
  return count;
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

void write_corrections(JsonWriter& w, const std::string& flat_label,
                       const std::optional<FlatFieldCorrectionSummary>& flat,
                       const std::optional<WhiteBalanceGains>& wb,
                       const std::string& wb_policy) {
  w.begin_object();
  w.key("flat_field");
  if (flat) {
    w.begin_object();
    w.key("path");
    w.value(flat_label);
    w.key("source");
    w.value("bilinear_demosaic_black_subtracted_raw");
    w.key("normalizer");
    write_rgb(w, flat->normalizer);
    w.key("floor_value");
    w.value(flat->floor_value);
    w.key("pixel_count");
    w.value(static_cast<std::int64_t>(flat->pixel_count));
    w.key("valid_sample_count");
    w.value(static_cast<std::int64_t>(flat->valid_sample_count));
    w.key("clamped_sample_count");
    w.value(static_cast<std::int64_t>(flat->clamped_sample_count));
    w.key("near_ceiling_sample_count");
    w.value(static_cast<std::int64_t>(flat->near_ceiling_sample_count));
    w.key("near_ceiling_fraction");
    w.value(flat->near_ceiling_fraction);
    w.key("max_allowed_near_ceiling_fraction");
    w.value(flat->max_allowed_near_ceiling_fraction);
    w.end_object();
  } else {
    w.null();
  }

  w.key("white_balance");
  if (wb) {
    w.begin_object();
    w.key("policy");
    w.value(wb_policy);
    w.key("gains");
    write_rgb(w, {wb->r, wb->g, wb->b});
    w.end_object();
  } else {
    w.begin_object();
    w.key("policy");
    w.value("none");
    w.end_object();
  }
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
  w.value("per_channel_pearson_direct_error_and_affine_fit");
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
    w.key("mean_error_before_affine");
    w.value(c.mean_error_before_affine);
    w.key("rmse_before_affine");
    w.value(c.rmse_before_affine);
    w.key("max_abs_error_before_affine");
    w.value(c.max_abs_error_before_affine);
    w.key("rmse_after_affine");
    w.value(c.rmse_after_affine);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_report(std::ostream& os, const std::string& file_label,
                  const std::string& coords_label,
                  const std::string& coordinate_source_format,
                  const RawCfaImage& cfa, const std::string& flat_label,
                  const std::optional<FlatFieldCorrectionSummary>& flat,
                  const std::optional<WhiteBalanceGains>& wb,
                  const std::string& wb_policy,
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
  w.key("coordinate_source_format");
  w.value(coordinate_source_format);
  w.key("extraction_coordinate_convention");
  w.value("one_based_top_left_rectangles_after_source_conversion");
  w.key("rgb_source");
  w.value("bilinear_demosaic_black_subtracted_raw");
  w.key("corrections");
  write_corrections(w, flat_label, flat, wb, wb_policy);
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
  if (!flat) {
    w.value("No sphere flat-field correction was applied");
  }
  if (!wb) {
    w.value("No white-balance gains were applied");
  }
  if (comparison && (flat || wb)) {
    w.value(
        "Reference RGB comparison uses caller-supplied values; they must match "
        "the applied correction state");
  }
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
    } else if (arg == "--flat-field-raw") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --flat-field-raw requires a path\n";
        return 2;
      }
      args.flat_field_raw = argv[i];
    } else if (arg == "--flat-field-floor") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --flat-field-floor requires a value\n";
        return 2;
      }
      try {
        std::size_t consumed = 0;
        args.flat_field_floor = std::stod(argv[i], &consumed);
        if (consumed != std::string(argv[i]).size() ||
            !std::isfinite(args.flat_field_floor) ||
            args.flat_field_floor <= 0) {
          throw std::runtime_error("");
        }
      } catch (...) {
        std::cerr << "camera_iq patches: --flat-field-floor must be positive\n";
        return 2;
      }
    } else if (arg == "--wb-gains") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --wb-gains requires R,G,B\n";
        return 2;
      }
      try {
        args.wb_gains = parse_wb_gains(argv[i]);
      } catch (const std::exception& ex) {
        std::cerr << "camera_iq patches: " << ex.what() << "\n";
        return 2;
      }
    } else if (arg == "--wb-from-flat-field") {
      args.wb_from_flat_field = true;
    } else if (arg == "--rgb-csv-out") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --rgb-csv-out requires a path\n";
        return 2;
      }
      args.rgb_csv_out = argv[i];
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
  if (args.wb_from_flat_field && args.wb_gains) {
    std::cerr << "camera_iq patches: use either --wb-gains or "
                 "--wb-from-flat-field, not both\n";
    return 2;
  }
  if (args.wb_from_flat_field && args.flat_field_raw.empty()) {
    std::cerr << "camera_iq patches: --wb-from-flat-field requires "
                 "--flat-field-raw\n";
    return 2;
  }
  const bool corrections_requested =
      !args.flat_field_raw.empty() || args.wb_gains.has_value() ||
      args.wb_from_flat_field;

  try {
    std::filesystem::path actual_raw = args.raw_file;
    std::string file_label = args.raw_file.string();
    std::filesystem::path actual_coords = args.coords;
    std::string coords_label = args.coords.string();
    std::string coordinate_source_format =
        "checker2colors_csv_one_based_top_left";
    std::filesystem::path actual_rawdigger_csv;
    std::string rawdigger_label;
    std::filesystem::path actual_reference_rgb;
    std::string reference_label;
    std::filesystem::path actual_flat_field_raw;
    std::string flat_label;

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
        coordinate_source_format = "rawdigger_csv_zero_based_left_top";
      }

      if (args.reference_rgb.empty() && args.rawdigger_csv.empty()) {
        const auto configs = read_dataset_config(args.config);
        const auto it = configs.find(args.dataset_id);
        if (it != configs.end() && it->second.color_reference &&
            !it->second.color_reference->pairing_rgb_path.empty()) {
          args.reference_rgb = it->second.color_reference->pairing_rgb_path;
        }
      }
      if (!args.flat_field_raw.empty()) {
        const auto resolved_flat =
            resolve_dataset_side_path(*dataset, args.flat_field_raw);
        actual_flat_field_raw = resolved_flat.actual;
        flat_label = resolved_flat.label;
      }
    }

    if (!args.rawdigger_csv.empty() && !dataset) {
      actual_rawdigger_csv = args.rawdigger_csv;
      rawdigger_label = args.rawdigger_csv.string();
      coords_label = rawdigger_label;
      coordinate_source_format = "rawdigger_csv_zero_based_left_top";
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
    if (!args.flat_field_raw.empty() && !dataset) {
      actual_flat_field_raw = args.flat_field_raw;
      flat_label = args.flat_field_raw.string();
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
    std::vector<RgbPixel> corrected_rgb = rgb;
    std::optional<FlatFieldCorrectionSummary> flat_summary;
    std::optional<WhiteBalanceGains> applied_wb = args.wb_gains;
    std::string wb_policy = "none";
    if (args.wb_gains) {
      wb_policy = args.flat_field_raw.empty()
                      ? "explicit_channel_gains"
                      : "explicit_channel_gains_after_flat_field";
    }
    if (!actual_flat_field_raw.empty()) {
      const auto flat_cfa = read_raw_cfa_image(actual_flat_field_raw);
      if (!flat_cfa) {
        std::cerr << "camera_iq patches: cannot read/unpack " << flat_label
                  << "\n";
        return 1;
      }
      if (flat_cfa->width != cfa->width || flat_cfa->height != cfa->height) {
        std::cerr << "camera_iq patches: flat-field dimensions do not match "
                  << "the target RAW\n";
        return 1;
      }
      if (flat_cfa->meta.make != cfa->meta.make ||
          flat_cfa->meta.model != cfa->meta.model ||
          flat_cfa->meta.cfa_pattern != cfa->meta.cfa_pattern) {
        std::cerr << "camera_iq patches: flat-field camera/CFA metadata do "
                  << "not match the target RAW\n";
        return 1;
      }
      const auto flat_rgb =
          demosaic_bilinear(flat_cfa->samples.data(), flat_cfa->width,
                            flat_cfa->height, flat_cfa->row_stride_pixels,
                            flat_cfa->color_at_position, flat_cfa->cdesc);
      if (flat_rgb.empty()) {
        std::cerr << "camera_iq patches: unsupported flat-field CFA for "
                  << flat_label << "\n";
        return 1;
      }
      FlatFieldCorrectionSummary summary;
      corrected_rgb =
          apply_flat_field(corrected_rgb, flat_rgb, cfa->width, cfa->height,
                           args.flat_field_floor, &summary);
      const auto ceiling = residual_ceiling_by_rgb_channel(flat_cfa->meta);
      summary.near_ceiling_sample_count = count_flat_samples_near_ceiling(
          flat_rgb, ceiling, kFlatNearCeilingFraction);
      const double total_samples =
          static_cast<double>(flat_rgb.size()) * 3.0;
      summary.near_ceiling_fraction =
          total_samples > 0
              ? static_cast<double>(summary.near_ceiling_sample_count) /
                    total_samples
              : 0.0;
      summary.max_allowed_near_ceiling_fraction =
          kMaxFlatNearCeilingFraction;
      if (summary.near_ceiling_fraction > kMaxFlatNearCeilingFraction) {
        std::cerr << "camera_iq patches: flat-field RAW is too close to the "
                  << "sensor ceiling for correction\n";
        return 1;
      }
      flat_summary = summary;
    }
    if (args.wb_from_flat_field) {
      if (!flat_summary) {
        throw std::runtime_error(
            "white balance: flat-field summary was not computed");
      }
      applied_wb = white_balance_gains_from_flat_field(*flat_summary);
      wb_policy = "flat_field_green_anchor";
    }
    if (applied_wb) {
      corrected_rgb = apply_white_balance(corrected_rgb, *applied_wb);
    }
    std::vector<PatchCoord> coords;
    std::vector<std::string> sample_names;
    std::optional<std::vector<CameraRgbPatch>> embedded_reference_rgb;
    if (!actual_rawdigger_csv.empty()) {
      const auto rawdigger = read_rawdigger_patch_table(
          actual_rawdigger_csv, actual_raw.filename().string());
      coords = rawdigger.coords;
      sample_names = rawdigger.sample_names;
      if (actual_reference_rgb.empty() && !corrections_requested) {
        embedded_reference_rgb = rawdigger.reference_rgb;
        reference_label = rawdigger_label + "#Ravg/Gavg/Bavg";
      }
    } else {
      coords = read_patch_coords_csv(actual_coords);
    }
    const auto patches =
        extract_patch_means(corrected_rgb, cfa->width, cfa->height, coords);

    std::optional<PatchComparison> comparison;
    if (!actual_reference_rgb.empty()) {
      comparison = compare_patch_means_to_rgb(
          patches, read_camera_rgb_csv(actual_reference_rgb));
    } else if (embedded_reference_rgb) {
      comparison = compare_patch_means_to_rgb(patches, *embedded_reference_rgb);
    }
    if (!args.rgb_csv_out.empty()) {
      std::ofstream os(args.rgb_csv_out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq patches: cannot write " << args.rgb_csv_out
                  << "\n";
        return 1;
      }
      write_camera_rgb_csv(os, patches);
    }

    if (args.out.empty()) {
      write_report(std::cout, file_label, coords_label,
                   coordinate_source_format, *cfa, flat_label, flat_summary,
                   applied_wb, wb_policy, patches, sample_names, comparison,
                   reference_label);
      std::cout << "\n";
    } else {
      std::ofstream os(args.out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq patches: cannot write " << args.out << "\n";
        return 1;
      }
      write_report(os, file_label, coords_label, coordinate_source_format, *cfa,
                   flat_label, flat_summary, applied_wb, wb_policy, patches,
                   sample_names, comparison, reference_label);
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
