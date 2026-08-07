#include "camera_iq/commands.hpp"

#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"
#include "camera_iq/spectral_compare.hpp"
#include "camera_iq/spectral_series.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace camera_iq {
namespace {

constexpr std::size_t kMaxSpectralGridSamples = 1'000'000;

void usage(std::ostream& output) {
  output <<
      "Usage: camera_iq spectro-compare SERIES.csv [options]\n\n"
      "Compare two repeated spectral series after resampling and normalization\n"
      "on one explicit common grid.\n"
      "No acquisition conditions or causal interpretation are inferred.\n\n"
      "Required:\n"
      "  --reference ID       Series used in the relative-L2 denominator\n"
      "  --candidate ID       Series compared with the reference\n"
      "  --common-start NM    First common-grid wavelength\n"
      "  --common-end NM      Last common-grid wavelength\n"
      "  --common-step NM     Common-grid increment\n\n"
      "Optional:\n"
      "  --exclude NM[,NM]    Report a residual excluding diagnostic bands\n"
      "  --offset-min NM      Minimum selected-series wavelength offset\n"
      "  --offset-max NM      Maximum selected-series wavelength offset\n"
      "  --offset-step NM     Offset sweep step; zero disables the sweep\n"
      "  --offset-series NAME reference or candidate (default candidate)\n"
      "  --out-json FILE      Write structured comparison JSON\n"
      "  --out-csv FILE       Write per-band CSV\n"
      "  -h, --help           Show this help\n";
}

double number(std::string_view text, std::string_view option) {
  try {
    std::size_t consumed = 0;
    const std::string owned(text);
    const double value = std::stod(owned, &consumed);
    if (consumed != owned.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (...) {
    throw std::runtime_error(std::string(option) +
                             " requires a finite number");
  }
}

std::vector<double> number_list(std::string_view text,
                                std::string_view option) {
  std::vector<double> result;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const std::size_t end = text.find(',', begin);
    const std::string_view item = text.substr(begin, end - begin);
    if (item.empty()) {
      throw std::runtime_error(std::string(option) +
                               " contains an empty value");
    }
    result.push_back(number(item, option));
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
  return result;
}

std::vector<double> regular_grid(double start, double end, double step) {
  if (step <= 0.0 || end < start) {
    throw std::runtime_error("common grid requires START <= END and STEP > 0");
  }
  const double span = end - start;
  const double intervals = span / step;
  const double rounded = std::round(intervals);
  if (!std::isfinite(span) || !std::isfinite(intervals) ||
      !std::isfinite(rounded) || rounded < 1.0 ||
      rounded + 1.0 > static_cast<double>(kMaxSpectralGridSamples) ||
      std::fabs(intervals - rounded) > 1e-9) {
    throw std::runtime_error(
        "common grid must contain 2 to 1000000 finite, evenly spaced samples");
  }
  std::vector<double> result;
  result.reserve(static_cast<std::size_t>(rounded) + 1);
  for (std::size_t index = 0; index <= static_cast<std::size_t>(rounded);
       ++index) {
    result.push_back(start + static_cast<double>(index) * step);
  }
  return result;
}

const SpectralSeries& find_series(const std::vector<SpectralSeries>& series,
                                  const std::string& id) {
  const auto found = std::find_if(series.begin(), series.end(),
                                  [&](const auto& item) {
                                    return item.id == id;
                                  });
  if (found == series.end()) {
    throw std::runtime_error("series '" + id + "' was not found");
  }
  return *found;
}

void optional_number(JsonWriter& writer, const std::optional<double>& value) {
  if (value) {
    writer.value(*value);
  } else {
    writer.null();
  }
}

void write_group(JsonWriter& writer, const SampledSpectrumGroupAnalysis& group,
                 const SampledSpectrum& first) {
  writer.begin_object();
  writer.key("reading_count");
  writer.value(static_cast<std::int64_t>(group.count));
  writer.key("native_start_nm");
  writer.value(first.wavelength_nm.front());
  writer.key("native_end_nm");
  writer.value(first.wavelength_nm.back());
  writer.key("native_step_nm");
  writer.value(group.wavelength_step_nm);
  writer.key("level_coefficient_of_variation");
  optional_number(writer, group.coefficient_of_variation);
  writer.key("max_normalized_shape_relative_l2");
  optional_number(writer, group.max_shape_relative_l2);
  writer.end_object();
}

void write_json(std::ostream& output, const SpectralComparison& comparison,
                const SpectralSeries& reference,
                const SpectralSeries& candidate) {
  JsonWriter writer(output);
  writer.begin_object();
  writer.key("schema_version");
  writer.value(1);
  writer.key("reference_id");
  writer.value(reference.id);
  writer.key("candidate_id");
  writer.value(candidate.id);
  writer.key("normalization");
  writer.value(comparison.normalization);
  writer.key("interpolation");
  writer.value(comparison.interpolation);
  writer.key("relative_l2_denominator");
  writer.value(comparison.relative_l2_denominator);
  writer.key("offset_convention");
  writer.value(comparison.offset_convention);
  writer.key("reference_group");
  write_group(writer, comparison.reference_group, reference.readings.front());
  writer.key("candidate_group");
  write_group(writer, comparison.candidate_group, candidate.readings.front());
  writer.key("directional_relative_l2");
  writer.value(comparison.directional_relative_l2);
  writer.key("bands");
  writer.begin_array();
  for (const auto& band : comparison.bands) {
    writer.begin_object();
    writer.key("wavelength_nm");
    writer.value(band.wavelength_nm);
    writer.key("signed_residual");
    writer.value(band.signed_residual);
    writer.key("squared_residual_fraction");
    writer.value(band.squared_residual_fraction);
    writer.end_object();
  }
  writer.end_array();
  writer.key("diagnostic_exclusions");
  writer.begin_array();
  for (const auto& exclusion : comparison.exclusion_results) {
    writer.begin_object();
    writer.key("excluded_wavelength_nm");
    writer.begin_array();
    for (const double wavelength : exclusion.excluded_wavelength_nm) {
      writer.value(wavelength);
    }
    writer.end_array();
    writer.key("retained_sample_count");
    writer.value(static_cast<std::int64_t>(exclusion.retained_sample_count));
    writer.key("directional_relative_l2");
    writer.value(exclusion.directional_relative_l2);
    writer.end_object();
  }
  writer.end_array();
  writer.key("offset_sensitivity_sample_count");
  writer.value(static_cast<std::int64_t>(
      comparison.offset_sensitivity_sample_count));
  writer.key("zero_offset_directional_relative_l2");
  optional_number(writer, comparison.zero_offset_directional_relative_l2);
  writer.key("offset_sensitivity");
  writer.begin_array();
  for (const auto& item : comparison.offset_sensitivity) {
    writer.begin_object();
    writer.key("wavelength_offset_nm");
    writer.value(item.wavelength_offset_nm);
    writer.key("directional_relative_l2");
    writer.value(item.directional_relative_l2);
    writer.end_object();
  }
  writer.end_array();
  writer.key("best_wavelength_offset_nm");
  if (comparison.offset_sensitivity.empty()) {
    writer.null();
  } else {
    writer.value(comparison.best_wavelength_offset_nm);
  }
  writer.key("best_offset_directional_relative_l2");
  if (comparison.offset_sensitivity.empty()) {
    writer.null();
  } else {
    writer.value(comparison.best_offset_directional_relative_l2);
  }
  writer.key("best_offset_bands");
  writer.begin_array();
  for (const auto& band : comparison.best_offset_bands) {
    writer.begin_object();
    writer.key("wavelength_nm");
    writer.value(band.wavelength_nm);
    writer.key("signed_residual");
    writer.value(band.signed_residual);
    writer.key("squared_residual_fraction");
    writer.value(band.squared_residual_fraction);
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
}

void write_csv(std::ostream& output, const SpectralComparison& comparison) {
  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  output << "wavelength_nm,reference_normalized,candidate_normalized,"
            "signed_residual,squared_residual_fraction\n";
  for (std::size_t index = 0; index < comparison.bands.size(); ++index) {
    const auto& band = comparison.bands[index];
    output << band.wavelength_nm << ','
           << comparison.reference_on_common_grid[index] << ','
           << comparison.candidate_on_common_grid[index] << ','
           << band.signed_residual << ',' << band.squared_residual_fraction
           << '\n';
  }
}

}  // namespace

int cmd_spectro_compare(int argc, char** argv) {
  if (argc == 1 && (std::string_view(argv[0]) == "--help" ||
                    std::string_view(argv[0]) == "-h")) {
    usage(std::cout);
    return 0;
  }
  if (argc < 1) {
    usage(std::cerr);
    return 2;
  }
  const std::filesystem::path input = argv[0];
  std::string reference_id;
  std::string candidate_id;
  std::filesystem::path out_json;
  std::filesystem::path out_csv;
  std::optional<double> common_start;
  std::optional<double> common_end;
  std::optional<double> common_step;
  SpectralComparisonOptions options;

  try {
    for (int index = 1; index < argc; ++index) {
      const std::string_view option = argv[index];
      if (option == "--help" || option == "-h") {
        usage(std::cout);
        return 0;
      }
      if (index + 1 >= argc) {
        throw std::runtime_error(std::string(option) + " requires a value");
      }
      const std::string_view value = argv[++index];
      if (option == "--reference") reference_id = value;
      else if (option == "--candidate") candidate_id = value;
      else if (option == "--common-start") common_start = number(value, option);
      else if (option == "--common-end") common_end = number(value, option);
      else if (option == "--common-step") common_step = number(value, option);
      else if (option == "--exclude") options.excluded_wavelength_nm = number_list(value, option);
      else if (option == "--offset-min") options.offset_min_nm = number(value, option);
      else if (option == "--offset-max") options.offset_max_nm = number(value, option);
      else if (option == "--offset-step") options.offset_step_nm = number(value, option);
      else if (option == "--offset-series") {
        if (value == "reference") options.offset_series = SpectralOffsetSeries::Reference;
        else if (value == "candidate") options.offset_series = SpectralOffsetSeries::Candidate;
        else throw std::runtime_error("--offset-series must be reference or candidate");
      }
      else if (option == "--out-json") out_json = value;
      else if (option == "--out-csv") out_csv = value;
      else throw std::runtime_error("unknown option '" + std::string(option) + "'");
    }
    if (reference_id.empty() || candidate_id.empty() ||
        reference_id == candidate_id || !common_start || !common_end ||
        !common_step) {
      throw std::runtime_error(
          "distinct reference/candidate IDs and a complete common grid are required");
    }
    options.common_wavelength_nm =
        regular_grid(*common_start, *common_end, *common_step);
    if (output_path_aliases_input(out_json, input) ||
        output_path_aliases_input(out_csv, input) ||
        (!out_json.empty() && !out_csv.empty() &&
         output_path_aliases_input(out_json, out_csv))) {
      std::cerr << "camera_iq spectro-compare: input and output paths must differ\n";
      return 2;
    }
  } catch (const std::runtime_error& error) {
    std::cerr << "camera_iq spectro-compare: " << error.what() << '\n';
    return 2;
  }

  try {
    const auto series = read_spectral_series_csv_file(input);
    const auto& reference = find_series(series, reference_id);
    const auto& candidate = find_series(series, candidate_id);
    const auto comparison =
        compare_spectral_groups(reference.readings, candidate.readings, options);
    if (out_json.empty()) {
      write_json(std::cout, comparison, reference, candidate);
      std::cout << '\n';
    } else if (!write_output_file_checked(
                   out_json, "spectro-compare",
                   [&](std::ostream& output) {
                     write_json(output, comparison, reference, candidate);
                   },
                   std::cerr)) {
      return 1;
    }
    if (!out_csv.empty() &&
        !write_output_file_checked(
            out_csv, "spectro-compare",
            [&](std::ostream& output) { write_csv(output, comparison); },
            std::cerr, false)) {
      return 1;
    }
    std::cerr << "spectro-compare: compared " << reference.readings.size()
              << " reference and " << candidate.readings.size()
              << " candidate readings on " << comparison.bands.size()
              << " wavelengths\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "camera_iq spectro-compare: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace camera_iq
