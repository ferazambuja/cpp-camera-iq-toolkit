#include "camera_iq/commands.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/dataset_config.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"
#include "camera_iq/raw_meta.hpp"
#include "camera_iq/shading.hpp"

namespace camera_iq {
namespace {

// Parses a finite double in (lo, hi]. Trailing garbage is an error, so "0.2x"
// does not silently become 0.2.
bool parse_fraction(std::string_view text, double lo, double hi, double& out) {
  const std::string s(text);
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0') return false;
  if (!std::isfinite(v) || v <= lo || v > hi) return false;
  out = v;
  return true;
}

bool parse_closed_value(std::string_view text, double lo, double hi,
                        double& out) {
  const std::string s(text);
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || *end != '\0' || !std::isfinite(v) || v < lo ||
      v > hi) {
    return false;
  }
  out = v;
  return true;
}

bool parse_positive_int(std::string_view text, int& out) {
  const std::string s(text);
  char* end = nullptr;
  const long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0') return false;
  if (v <= 0 || v > 100000) return false;
  out = static_cast<int>(v);
  return true;
}

bool parse_nonnegative_int(std::string_view text, int& out) {
  const std::string s(text);
  char* end = nullptr;
  const long v = std::strtol(s.c_str(), &end, 10);
  if (end == s.c_str() || *end != '\0' || v < 0 || v > 100000) return false;
  out = static_cast<int>(v);
  return true;
}

// "16x12" — both dimensions required and positive. A bare "16" is rejected
// rather than guessed at.
bool parse_grid(std::string_view text, int& cols, int& rows) {
  const std::size_t x = text.find('x');
  if (x == std::string_view::npos) return false;
  return parse_positive_int(text.substr(0, x), cols) &&
         parse_positive_int(text.substr(x + 1), rows);
}

void write_plane_array(JsonWriter& w, const std::array<double, 4>& values) {
  w.begin_array();
  for (const double v : values) w.value(v);
  w.end_array();
}

// Non-finite entries serialize as null: NaN marks a bin where the ratio is
// undefined, and JSON has no NaN.
void write_map(JsonWriter& w, const std::vector<double>& values) {
  w.begin_array();
  for (const double v : values) {
    if (std::isfinite(v)) {
      w.value(v);
    } else {
      w.null();
    }
  }
  w.end_array();
}

// Referenced files are published as basenames. The private archive tree is not
// part of the measurement, and `src/cmd_sfr.cpp` already applies this rule to
// its advisory tables.
std::string basename_of(std::string_view path) {
  return std::filesystem::path(std::string(path)).filename().string();
}

// Dataset-relative labels ("Images/Sphere/x.RAF") pass through; an absolute
// path is reduced to its basename. Publishing the shape of a private archive
// tree is what the dataset-label convention exists to prevent.
std::string publication_label(std::string_view file_label) {
  const std::filesystem::path p{std::string(file_label)};
  return p.is_absolute() ? p.filename().string() : std::string(file_label);
}

void write_rect(JsonWriter& w, const RoiRect& rect) {
  w.begin_object();
  w.key("x");
  w.value(rect.x);
  w.key("y");
  w.value(rect.y);
  w.key("width");
  w.value(rect.width);
  w.key("height");
  w.value(rect.height);
  w.end_object();
}

void csv_row(std::ostream& os, std::string_view file_label,
             std::string_view channel, int cfa_position, std::string_view metric,
             std::string_view bin_row, std::string_view bin_col, double value,
             std::string_view units, bool accepted) {
  os << publication_label(file_label) << ',' << channel << ',' << cfa_position
     << ',' << metric
     << ',' << bin_row << ',' << bin_col << ',' << value << ',' << units << ','
     << (accepted ? "true" : "false") << '\n';
}

}  // namespace

void write_shading_json(std::ostream& os, std::string_view file_label,
                        const ShadingField& field,
                        const ShadingChromatic& chroma,
                        const ShadingPedestal& pedestal) {
  JsonWriter w(os);
  w.begin_object();
  w.key("file");
  w.value(publication_label(file_label));
  w.key("accepted");
  w.value(field.valid);
  w.key("rejection_reason");
  if (field.rejection_reason.empty()) {
    w.null();
  } else {
    w.value(field.rejection_reason);
  }
  w.key("signal_ceiling_dn");
  write_plane_array(w, field.signal_ceiling);

  w.key("grid");
  w.begin_object();
  w.key("cols");
  w.value(field.grid_cols);
  w.key("rows");
  w.value(field.grid_rows);
  w.end_object();

  w.key("geometry");
  if (field.geometry.valid) {
    w.begin_object();
    w.key("gate");
    write_rect(w, field.geometry.gate);
    w.key("center");
    write_rect(w, field.geometry.center);
    w.key("corners");
    w.begin_array();
    for (const RoiRect& corner : field.geometry.corners) write_rect(w, corner);
    w.end_array();
    w.end_object();
  } else {
    w.null();
  }

  // Measured diagnostics. These survive rejection: the numbers that rejected a
  // frame are what make the rejection reviewable.
  w.key("gates");
  w.begin_object();
  w.key("near_ceiling_fraction_gate");
  write_plane_array(w, field.gates.near_ceiling_frac_gate);
  w.key("near_ceiling_fraction_frame");
  write_plane_array(w, field.gates.near_ceiling_frac_frame);
  w.key("negative_fraction");
  write_plane_array(w, field.gates.negative_frac);
  w.key("center_signal_fraction");
  write_plane_array(w, field.gates.center_signal_frac);
  w.key("min_bin_coverage");
  w.value(field.gates.min_bin_coverage);
  w.key("near_ceiling_ok");
  w.value(field.gates.near_ceiling_ok);
  w.key("low_signal_ok");
  w.value(field.gates.low_signal_ok);
  w.key("negative_ok");
  w.value(field.gates.negative_ok);
  w.key("coverage_ok");
  w.value(field.gates.coverage_ok);
  w.key("finite_ok");
  w.value(field.gates.finite_ok);
  w.end_object();

  w.key("center_block_median");
  write_plane_array(w, field.center_block_median);

  w.key("corner_median");
  w.begin_array();
  for (int q = 0; q < 4; ++q) {
    write_plane_array(w, field.blocks.corner_median[q]);
  }
  w.end_array();

  // Measurement is not verification: pairing and tolerance verdicts remain
  // explicit so a merely readable dark cannot silently clear the flag.
  w.key("pedestal");
  w.begin_object();
  w.key("pedestal_unverified");
  w.value(!pedestal.verified);
  if (pedestal.measured) {
    w.key("dark_file");
    w.value(basename_of(pedestal.dark_file));
    w.key("residual_dn");
    write_plane_array(w, pedestal.residual_dn);
    w.key("max_abs_residual_dn");
    w.value(pedestal.max_abs_residual_dn);
    w.key("exposure_metadata_matches");
    w.value(pedestal.exposure_metadata_matches);
  }
  w.key("compatible");
  w.value(pedestal.compatible);
  w.key("within_tolerance");
  w.value(pedestal.within_tolerance);
  w.key("verified");
  w.value(pedestal.verified);
  w.end_object();

  // Derived results. Null when the measurement was rejected: an unnormalizable
  // frame has no relative response, and emitting one anyway would publish a
  // gate failure as a measurement.
  w.key("relative_response");
  if (field.valid) {
    w.begin_array();
    for (int p = 0; p < 4; ++p) write_map(w, field.relative[p]);
    w.end_array();
  } else {
    w.null();
  }

  w.key("corner_relative");
  if (field.valid) {
    w.begin_array();
    for (int q = 0; q < 4; ++q) {
      write_plane_array(w, field.blocks.corner_relative[q]);
    }
    w.end_array();
  } else {
    w.null();
  }

  const bool chromatic_available = field.valid && chroma.valid;
  w.key("chromatic_complete");
  w.value(chromatic_available && chroma.complete);
  w.key("missing_chromatic_bin_count");
  w.value(static_cast<long long>(chroma.missing_bin_count));
  w.key("cfa_positions");
  if (chromatic_available) {
    w.begin_object();
    w.key("r");
    w.value(chroma.red_position);
    w.key("g1");
    w.value(chroma.green1_position);
    w.key("g2");
    w.value(chroma.green2_position);
    w.key("b");
    w.value(chroma.blue_position);
    w.end_object();
  } else {
    w.null();
  }
  w.key("c_rg");
  if (chromatic_available) {
    write_map(w, chroma.c_rg);
  } else {
    w.null();
  }
  w.key("c_bg");
  if (chromatic_available) {
    write_map(w, chroma.c_bg);
  } else {
    w.null();
  }
  w.key("c_g1g2");
  if (chromatic_available) {
    write_map(w, chroma.c_g1g2);
  } else {
    w.null();
  }

  // A travels with every luminance figure, so it is serialized beside its
  // policy verdict rather than left for a caller to recompute.
  w.key("green_asymmetry");
  if (chromatic_available) {
    w.value(chroma.green_asymmetry);
  } else {
    w.null();
  }
  w.key("asymmetry_exceeds_policy");
  w.value(chromatic_available && chroma.asymmetry_exceeds_policy);
  w.key("attribution");
  w.value(chromatic_available && chroma.asymmetry_exceeds_policy
              ? "source-lens-sensor composite; lens-only attribution refused"
              : "source-lens-sensor composite");

  w.end_object();
}

void write_shading_comparison_json(
    std::ostream& os, std::string_view primary_label,
    const ShadingField& primary_field,
    const ShadingChromatic& primary_chroma,
    const ShadingPedestal& primary_pedestal, std::string_view repeat_label,
    const ShadingField& repeat_field, const ShadingChromatic& repeat_chroma,
    const ShadingPedestal& repeat_pedestal,
    const ShadingComparison& comparison) {
  std::ostringstream primary;
  std::ostringstream repeat;
  write_shading_json(primary, primary_label, primary_field, primary_chroma,
                     primary_pedestal);
  write_shading_json(repeat, repeat_label, repeat_field, repeat_chroma,
                     repeat_pedestal);
  os << "{\"primary\":" << primary.str() << ",\"repeat\":"
     << repeat.str() << ",\"comparison\":{\"measured\":"
     << (comparison.measured ? "true" : "false")
     << ",\"max_corner_delta_pp\":" << comparison.max_corner_delta_pp
     << ",\"rms_corner_delta_pp\":" << comparison.rms_corner_delta_pp
     << "}}";
}

void write_shading_csv(std::ostream& os, std::string_view file_label,
                       const std::array<std::string, 4>& channel_labels,
                       const ShadingField& field,
                       const ShadingChromatic& chroma,
                       const ShadingPedestal& pedestal) {
  os << "file,channel,cfa_position,metric,bin_row,bin_col,value,units,accepted"
     << '\n';
  const bool ok = field.valid;

  // Diagnostics first: they are present whatever the verdict.
  for (int p = 0; p < 4; ++p) {
    const std::string& ch = channel_labels[p];
    csv_row(os, file_label, ch, p, "near_ceiling_fraction_gate", "", "",
            field.gates.near_ceiling_frac_gate[p], "fraction", ok);
    csv_row(os, file_label, ch, p, "near_ceiling_fraction_frame", "", "",
            field.gates.near_ceiling_frac_frame[p], "fraction", ok);
    csv_row(os, file_label, ch, p, "negative_fraction", "", "",
            field.gates.negative_frac[p], "fraction", ok);
    csv_row(os, file_label, ch, p, "center_signal_fraction", "", "",
            field.gates.center_signal_frac[p], "fraction", ok);
    csv_row(os, file_label, ch, p, "center_block_median", "", "",
            field.center_block_median[p], "dn", ok);
  }
  csv_row(os, file_label, "", -1, "min_bin_coverage", "", "",
          field.gates.min_bin_coverage, "fraction", ok);
  if (pedestal.measured) {
    csv_row(os, file_label, "", -1, "pedestal_max_abs_residual", "", "",
            pedestal.max_abs_residual_dn, "dn", ok);
  }

  if (!ok) return;

  // A is a measured value and belongs in the table beside the maps it qualifies.
  if (chroma.valid) {
    csv_row(os, file_label, "", -1, "green_asymmetry", "", "",
            chroma.green_asymmetry, "ratio", ok);
  }

  const auto emit_map = [&](const std::vector<double>& values,
                            std::string_view metric, const std::string& channel,
                            int position) {
    for (std::size_t i = 0; i < values.size(); ++i) {
      if (field.grid_cols <= 0) break;
      const int row = static_cast<int>(i) / field.grid_cols;
      const int col = static_cast<int>(i) % field.grid_cols;
      if (!std::isfinite(values[i])) continue;  // undefined bins are absent
      csv_row(os, file_label, channel, position, metric, std::to_string(row),
              std::to_string(col), values[i], "ratio", ok);
    }
  };

  for (int p = 0; p < 4; ++p) {
    emit_map(field.relative[p], "relative_response", channel_labels[p], p);
  }
  if (chroma.valid) {
    const std::string none;
    emit_map(chroma.c_rg, "c_rg", none, -1);
    emit_map(chroma.c_bg, "c_bg", none, -1);
    emit_map(chroma.c_g1g2, "c_g1g2", none, -1);
  }
}

int cmd_shading(int argc, char** argv) {
  std::filesystem::path file;
  std::filesystem::path dark;
  std::filesystem::path compare;
  std::filesystem::path compare_dark;
  std::string dataset_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path out;
  std::filesystem::path csv_out;
  ShadingOptions opts;

  for (int i = 0; i < argc; ++i) {
    const std::string_view arg = argv[i];
    const bool takes_value =
        arg == "--dark" || arg == "--compare" || arg == "--compare-dark" ||
        arg == "--dataset" || arg == "--config" || arg == "--out" ||
        arg == "--csv-out" ||
        arg == "--grid" || arg == "--gate-center-frac" ||
        arg == "--corner-block" || arg == "--corner-inset" ||
        arg == "--near-ceiling-level" || arg == "--near-ceiling-max" ||
        arg == "--min-center-signal" || arg == "--max-negative-frac" ||
        arg == "--min-bin-coverage" || arg == "--asymmetry-policy";
    if (takes_value && i + 1 >= argc) {
      std::cerr << "camera_iq shading: " << arg << " requires a value\n";
      return 2;
    }

    if (arg == "--dark") {
      dark = argv[++i];
    } else if (arg == "--compare") {
      compare = argv[++i];
    } else if (arg == "--compare-dark") {
      compare_dark = argv[++i];
    } else if (arg == "--dataset") {
      dataset_id = argv[++i];
    } else if (arg == "--config") {
      config = argv[++i];
    } else if (arg == "--out") {
      out = argv[++i];
    } else if (arg == "--csv-out") {
      csv_out = argv[++i];
    } else if (arg == "--grid") {
      if (!parse_grid(argv[++i], opts.grid_cols, opts.grid_rows)) {
        std::cerr << "camera_iq shading: --grid expects WxH with positive"
                     " dimensions\n";
        return 2;
      }
    } else if (arg == "--gate-center-frac") {
      if (!parse_fraction(argv[++i], 0.0, 1.0, opts.gate_center_frac)) {
        std::cerr << "camera_iq shading: --gate-center-frac expects a finite"
                     " fraction in (0, 1]\n";
        return 2;
      }
    } else if (arg == "--corner-block") {
      if (!parse_positive_int(argv[++i], opts.corner_block_px) ||
          opts.corner_block_px < 2) {
        std::cerr << "camera_iq shading: --corner-block expects a pixel count"
                     " of at least 2\n";
        return 2;
      }
    } else if (arg == "--corner-inset") {
      if (!parse_nonnegative_int(argv[++i], opts.corner_inset_px)) {
        std::cerr << "camera_iq shading: --corner-inset expects a non-negative"
                     " pixel count\n";
        return 2;
      }
    } else if (arg == "--near-ceiling-level") {
      if (!parse_fraction(argv[++i], 0.0, 1.0, opts.near_ceiling_level)) {
        std::cerr << "camera_iq shading: --near-ceiling-level expects a finite"
                     " fraction in (0, 1]\n";
        return 2;
      }
    } else if (arg == "--near-ceiling-max") {
      if (!parse_closed_value(argv[++i], 0.0, 1.0,
                              opts.near_ceiling_max)) {
        std::cerr << "camera_iq shading: --near-ceiling-max expects a finite"
                     " fraction in [0, 1]\n";
        return 2;
      }
    } else if (arg == "--min-center-signal") {
      if (!parse_closed_value(argv[++i], 0.0, 1.0,
                              opts.min_center_signal)) {
        std::cerr << "camera_iq shading: --min-center-signal expects a finite"
                     " fraction in [0, 1]\n";
        return 2;
      }
    } else if (arg == "--max-negative-frac") {
      if (!parse_closed_value(argv[++i], 0.0, 1.0,
                              opts.max_negative_frac)) {
        std::cerr << "camera_iq shading: --max-negative-frac expects a finite"
                     " fraction in [0, 1]\n";
        return 2;
      }
    } else if (arg == "--min-bin-coverage") {
      if (!parse_fraction(argv[++i], 0.0, 1.0, opts.min_bin_coverage)) {
        std::cerr << "camera_iq shading: --min-bin-coverage expects a finite"
                     " fraction in (0, 1]\n";
        return 2;
      }
    } else if (arg == "--asymmetry-policy") {
      if (!parse_closed_value(argv[++i], 0.0, 10.0,
                              opts.asymmetry_policy)) {
        std::cerr << "camera_iq shading: --asymmetry-policy expects a finite"
                     " non-negative value\n";
        return 2;
      }
    } else if (!arg.empty() && arg.front() == '-') {
      std::cerr << "camera_iq shading: unknown option '" << arg << "'\n";
      return 2;
    } else if (file.empty()) {
      file = arg;
    } else {
      std::cerr << "camera_iq shading: unexpected argument '" << arg << "'\n";
      return 2;
    }
  }

  if (file.empty()) {
    std::cerr << "Usage: camera_iq shading <raw-file> [--dark FILE]"
                 " [--compare FILE [--compare-dark FILE]] [--grid WxH]"
                 " [--gate-center-frac F] [--corner-block PX]"
                 " [--corner-inset PX] [--dataset ID] [--config FILE]"
                 " [--out FILE] [--csv-out FILE]\n";
    return 2;
  }
  // A comparison dark with no comparison frame means the caller believes a
  // comparison is happening. Ignoring the flag would hide that.
  if (!compare_dark.empty() && compare.empty()) {
    std::cerr << "camera_iq shading: --compare-dark requires --compare\n";
    return 2;
  }
  if (!dataset_id.empty() &&
      ((!dark.empty() && dark.is_absolute()) ||
       (!compare.empty() && compare.is_absolute()) ||
       (!compare_dark.empty() && compare_dark.is_absolute()))) {
    std::cerr << "camera_iq shading: --dataset requires relative primary, dark,"
                 " and comparison files\n";
    return 2;
  }

  std::filesystem::path root;
  std::filesystem::path actual_file = file;
  std::string file_label = file.string();
  if (!dataset_id.empty()) {
    const auto resolved = resolve_dataset_root(dataset_id, config);
    if (!resolved || !resolved->from_config) {
      std::cerr << "camera_iq shading: dataset id '" << dataset_id
                << "' not found in " << config << "\n";
      return 1;
    }
    if (file.is_absolute()) {
      std::cerr << "camera_iq shading: --dataset requires a relative file\n";
      return 2;
    }
    root = resolved->root;
    actual_file = root / file;
    file_label = dataset_file_label(dataset_id, file);
  }

  const auto image = read_raw_cfa_image(actual_file);
  if (!image) {
    std::cerr << "camera_iq shading: cannot read/unpack " << file_label << "\n";
    return 1;
  }

  // The production API owns metadata validation and signal-ceiling derivation.
  // The command cannot accidentally subtract black twice or gate signed
  // residuals against raw white.
  const ShadingAnalysis analysis = analyze_shading(*image, opts);
  const ShadingField& field = analysis.field;
  const ShadingChromatic& chroma = analysis.chromatic;

  ShadingPedestal pedestal;
  if (!dark.empty()) {
    const std::filesystem::path actual_dark = root.empty() ? dark : root / dark;
    const auto dark_image = read_raw_cfa_image(actual_dark);
    if (!dark_image) {
      std::cerr << "camera_iq shading: cannot read/unpack dark " << dark << "\n";
      return 1;
    }
    pedestal =
        verify_shading_pedestal(*image, *dark_image, dark.string(), 1.0);
  }

  std::optional<RawCfaImage> repeat_image;
  std::optional<ShadingAnalysis> repeat_analysis;
  ShadingPedestal repeat_pedestal;
  ShadingComparison comparison;
  std::string repeat_label;
  if (!compare.empty()) {
    const std::filesystem::path actual_compare =
        root.empty() ? compare : root / compare;
    repeat_label = dataset_id.empty()
                       ? compare.string()
                       : dataset_file_label(dataset_id, compare);
    repeat_image = read_raw_cfa_image(actual_compare);
    if (!repeat_image) {
      std::cerr << "camera_iq shading: cannot read/unpack comparison "
                << repeat_label << "\n";
      return 1;
    }
    repeat_analysis = analyze_shading(*repeat_image, opts);
    if (!compare_dark.empty()) {
      const std::filesystem::path actual_compare_dark =
          root.empty() ? compare_dark : root / compare_dark;
      const auto dark_image = read_raw_cfa_image(actual_compare_dark);
      if (!dark_image) {
        std::cerr << "camera_iq shading: cannot read/unpack comparison dark "
                  << compare_dark << "\n";
        return 1;
      }
      repeat_pedestal = verify_shading_pedestal(
          *repeat_image, *dark_image, compare_dark.string(), 1.0);
    }
    if (image->color_at_position == repeat_image->color_at_position &&
        image->cdesc == repeat_image->cdesc) {
      comparison = compare_shading_fields(field, repeat_analysis->field);
    }
  }

  if (!csv_out.empty()) {
    // Channel labels come from the file's own CFA descriptor, never a hardcoded
    // RGGB assumption. The two greens are numbered in mosaic-position order so
    // a C_G1G2 row can be traced back to the position it came from.
    std::array<std::string, 4> labels{};
    int green_seen = 0;
    for (int p = 0; p < 4; ++p) {
      const int index = image->color_at_position[p];
      const char letter =
          (index >= 0 && static_cast<std::size_t>(index) < image->cdesc.size())
              ? image->cdesc[static_cast<std::size_t>(index)]
              : '?';
      labels[p] = letter == 'G' ? std::string("G") + std::to_string(++green_seen)
                                : std::string(1, letter);
    }
    if (!write_output_file_checked(
            csv_out, "shading",
            [&](std::ostream& os) {
              write_shading_csv(os, file_label, labels, field, chroma, pedestal);
              if (repeat_analysis) {
                std::array<std::string, 4> repeat_labels{};
                int repeat_green_seen = 0;
                for (int p = 0; p < 4; ++p) {
                  const int index = repeat_image->color_at_position[p];
                  const char letter =
                      (index >= 0 && static_cast<std::size_t>(index) <
                                           repeat_image->cdesc.size())
                          ? repeat_image->cdesc[static_cast<std::size_t>(index)]
                          : '?';
                  repeat_labels[p] =
                      letter == 'G'
                          ? std::string("G") +
                                std::to_string(++repeat_green_seen)
                          : std::string(1, letter);
                }
                std::ostringstream repeat_csv;
                write_shading_csv(repeat_csv, repeat_label, repeat_labels,
                                  repeat_analysis->field,
                                  repeat_analysis->chromatic, repeat_pedestal);
                const std::string rows = repeat_csv.str();
                const std::size_t first_newline = rows.find('\n');
                if (first_newline != std::string::npos) {
                  os << rows.substr(first_newline + 1);
                }
                if (comparison.measured) {
                  csv_row(os, "comparison", "", -1, "max_corner_delta", "",
                          "", comparison.max_corner_delta_pp,
                          "percentage_points", true);
                  csv_row(os, "comparison", "", -1, "rms_corner_delta", "",
                          "", comparison.rms_corner_delta_pp,
                          "percentage_points", true);
                }
              }
            },
            std::cerr, /*append_newline=*/false)) {
      return 1;
    }
    std::cerr << "shading csv written to " << csv_out << "\n";
  }

  if (out.empty()) {
    if (repeat_analysis) {
      write_shading_comparison_json(
          std::cout, file_label, field, chroma, pedestal, repeat_label,
          repeat_analysis->field, repeat_analysis->chromatic, repeat_pedestal,
          comparison);
    } else {
      write_shading_json(std::cout, file_label, field, chroma, pedestal);
    }
    std::cout << "\n";
  } else {
    if (!write_output_file_checked(
            out, "shading",
            [&](std::ostream& os) {
              if (repeat_analysis) {
                write_shading_comparison_json(
                    os, file_label, field, chroma, pedestal, repeat_label,
                    repeat_analysis->field, repeat_analysis->chromatic,
                    repeat_pedestal, comparison);
              } else {
                write_shading_json(os, file_label, field, chroma, pedestal);
              }
            },
            std::cerr)) {
      return 1;
    }
    std::cerr << "shading written to " << out << "\n";
  }
  return 0;
}

}  // namespace camera_iq
