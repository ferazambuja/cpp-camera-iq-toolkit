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

#include "camera_iq/chart_localization.hpp"
#include "camera_iq/color_reference.hpp"
#include "camera_iq/dataset_config.hpp"
#include "camera_iq/demosaic.hpp"
#include "camera_iq/patches.hpp"
#include "camera_iq/raw_meta.hpp"

namespace camera_iq {
namespace {

constexpr double kMaxFlatNearCeilingFraction = 0.01;

struct Args {
  std::filesystem::path raw_file;
  std::string dataset_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path coords;
  std::filesystem::path rawdigger_csv;
  std::string sg_corners;
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
               " (--coords FILE | --rawdigger-csv FILE | --sg-corners SPEC)"
               " [--dataset ID] [--config FILE]"
               " [--reference-rgb FILE]"
               " [--flat-field-raw FILE] [--flat-field-floor DN]"
               " [--wb-gains R,G,B | --wb-from-flat-field]"
               " [--rgb-csv-out FILE] [--out FILE]\n"
               "--coords reads checker2colors-style x,y,width,height rows;\n"
               "--rawdigger-csv reads RAW-space RawDigger patch exports;\n"
               "--sg-corners reads TL;TR;BR;BL active-image corners as "
               "\"x1,y1;x2,y2;x3,y3;x4,y4\".\n";
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

PatchGeometryReport make_patch_geometry_report(
    const ChartLocalizationResult& geometry) {
  PatchGeometryReport out;
  out.chart_model = geometry.chart_model;
  out.method = geometry.method;
  out.corners = {PatchGeometryReportPoint{geometry.corners.top_left.x,
                                          geometry.corners.top_left.y},
                 PatchGeometryReportPoint{geometry.corners.top_right.x,
                                          geometry.corners.top_right.y},
                 PatchGeometryReportPoint{geometry.corners.bottom_right.x,
                                          geometry.corners.bottom_right.y},
                 PatchGeometryReportPoint{geometry.corners.bottom_left.x,
                                          geometry.corners.bottom_left.y}};
  out.patches.reserve(geometry.patches.size());
  for (const auto& patch : geometry.patches) {
    out.patches.push_back(
        PatchGeometryReportPatch{patch.reference_patch_id, patch.row,
                                 patch.column});
  }
  return out;
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
    } else if (arg == "--sg-corners") {
      if (++i >= argc) {
        std::cerr << "camera_iq patches: --sg-corners requires corners\n";
        return 2;
      }
      args.sg_corners = argv[i];
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

  const int coordinate_modes = (args.coords.empty() ? 0 : 1) +
                               (args.rawdigger_csv.empty() ? 0 : 1) +
                               (args.sg_corners.empty() ? 0 : 1);
  if (args.raw_file.empty() || coordinate_modes == 0) {
    usage();
    return 2;
  }
  if (coordinate_modes > 1) {
    std::cerr << "camera_iq patches: use exactly one of --coords, "
                 "--rawdigger-csv, or --sg-corners\n";
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
  std::optional<ChartLocalizationResult> sg_geometry;
  std::optional<PatchGeometryReport> sg_geometry_report;
  if (!args.sg_corners.empty()) {
    try {
      sg_geometry = localize_colorchecker_sg_grid(
          parse_colorchecker_sg_corners(args.sg_corners));
      sg_geometry_report = make_patch_geometry_report(*sg_geometry);
    } catch (const std::exception& ex) {
      std::cerr << "camera_iq patches: --sg-corners " << ex.what() << "\n";
      return 2;
    }
  }

  try {
    std::filesystem::path actual_raw = args.raw_file;
    std::string file_label = args.raw_file.string();
    std::filesystem::path actual_coords = args.coords;
    std::string coords_label = args.coords.string();
    std::string coordinate_source_format =
        "checker2colors_csv_one_based_top_left";
    if (sg_geometry) {
      coords_label = "manual:sg-corners";
      coordinate_source_format =
          "colorchecker_sg_corner_seeded_projective_grid";
    }
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
          flat_rgb, ceiling, flat_field_near_ceiling_threshold_fraction());
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
    } else if (sg_geometry) {
      coords = patch_coords_from_chart_geometry(*sg_geometry);
      sample_names = patch_ids_from_chart_geometry(*sg_geometry);
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
      write_patch_report_json(
          std::cout, file_label, coords_label, coordinate_source_format,
          cfa->meta, cfa->width, cfa->height, flat_label, flat_summary,
          applied_wb, wb_policy, patches, sample_names, comparison,
          reference_label, sg_geometry_report);
      std::cout << "\n";
    } else {
      std::ofstream os(args.out, std::ios::binary);
      if (!os) {
        std::cerr << "camera_iq patches: cannot write " << args.out << "\n";
        return 1;
      }
      write_patch_report_json(os, file_label, coords_label,
                              coordinate_source_format, cfa->meta, cfa->width,
                              cfa->height, flat_label, flat_summary, applied_wb,
                              wb_policy, patches, sample_names, comparison,
                              reference_label, sg_geometry_report);
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
