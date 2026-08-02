#include "camera_iq/commands.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/dataset_config.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"
#include "camera_iq/sha256.hpp"
#include "camera_iq/spectro_colorimetry.hpp"
#include "camera_iq/spectro_ingest.hpp"

namespace camera_iq {
namespace {

constexpr int kSpectroIngestSchemaVersion = 2;

struct Arguments {
  std::string root_or_id;
  std::filesystem::path config = default_dataset_config_path();
  std::filesystem::path ledger = "data/spectro_identity_ledger.csv";
  std::filesystem::path cmf = "data/cie1931_2deg_cmf_1nm.csv";
  std::filesystem::path json_out;
  std::filesystem::path groups_csv;
  std::filesystem::path spectra_csv;
  std::filesystem::path readings_csv;
  bool verify_aliases = false;
};

void usage(std::ostream &out) {
  out << "Usage: camera_iq spectro-ingest <dataset-root-or-id> [options]\n"
         "Options:\n"
         "  --config FILE       Dataset-ID configuration\n"
         "  --ledger FILE       Source-relative identity ledger\n"
         "  --cmf FILE          CIE XYZ colour-matching functions\n"
         "  --verify-aliases    Re-hash every declared byte-identical alias\n"
         "  --out FILE          Authoritative JSON output\n"
         "  --groups-csv FILE   One row per measurement group\n"
         "  --spectra-csv FILE  Absolute and normalized group spectra\n"
         "  --readings-csv FILE One row per canonical reading\n"
         "  -h, --help          Show this help\n";
}

std::optional<Arguments> parse_arguments(int argc, char **argv) {
  if (argc < 1)
    return std::nullopt;
  Arguments out;
  out.root_or_id = argv[0];
  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    if (option == "--verify-aliases") {
      out.verify_aliases = true;
      continue;
    }
    if (index + 1 >= argc)
      return std::nullopt;
    const std::filesystem::path value(argv[++index]);
    if (option == "--config") {
      out.config = value;
    } else if (option == "--ledger") {
      out.ledger = value;
    } else if (option == "--cmf") {
      out.cmf = value;
    } else if (option == "--out") {
      out.json_out = value;
    } else if (option == "--groups-csv") {
      out.groups_csv = value;
    } else if (option == "--spectra-csv") {
      out.spectra_csv = value;
    } else if (option == "--readings-csv") {
      out.readings_csv = value;
    } else {
      return std::nullopt;
    }
  }
  return out;
}

std::string read_file(const std::filesystem::path &path,
                      std::string_view description,
                      std::size_t max_input_bytes) {
  std::error_code size_error;
  const std::uintmax_t file_bytes =
      std::filesystem::file_size(path, size_error);
  if (size_error) {
    throw std::runtime_error("cannot inspect " + std::string(description) +
                             " " + path.string());
  }
  if (file_bytes > max_input_bytes) {
    throw std::runtime_error(std::string(description) +
                             " exceeds the input byte limit " +
                             path.string());
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + std::string(description) + " " +
                             path.string());
  }
  std::string bytes;
  bytes.reserve(static_cast<std::size_t>(file_bytes));
  std::array<char, 8192> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::size_t count = static_cast<std::size_t>(input.gcount());
    if (count > max_input_bytes - bytes.size()) {
      throw std::runtime_error(std::string(description) +
                               " exceeds the input byte limit " +
                               path.string());
    }
    bytes.append(buffer.data(), count);
  }
  if (input.bad()) {
    throw std::runtime_error("cannot read " + std::string(description) + " " +
                             path.string());
  }
  return bytes;
}

std::string binary64_le_sha256(const std::vector<double> &values) {
  static_assert(std::numeric_limits<double>::is_iec559,
                "spectro cross-check hashes require IEEE-754 binary64");
  std::string bytes;
  bytes.reserve(values.size() * sizeof(double));
  for (const double value : values) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (int shift = 0; shift < 64; shift += 8) {
      bytes.push_back(static_cast<char>((bits >> shift) & 0xFFu));
    }
  }
  return sha256_hex(bytes);
}

std::filesystem::path comparable(const std::filesystem::path &path) {
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(path, error);
  if (!error)
    return canonical;
  const std::filesystem::path absolute = std::filesystem::absolute(path, error);
  return error ? path.lexically_normal() : absolute.lexically_normal();
}

bool path_is_within(const std::filesystem::path &root,
                    const std::filesystem::path &candidate) {
  const std::filesystem::path relative =
      comparable(candidate).lexically_relative(comparable(root));
  if (relative.empty() || relative.is_absolute())
    return false;
  for (const auto &component : relative) {
    if (component == "..")
      return false;
  }
  return true;
}

void validate_output_paths(const Arguments &args,
                           const std::filesystem::path &dataset_root) {
  const std::vector<std::filesystem::path> outputs = {
      args.json_out, args.groups_csv, args.spectra_csv, args.readings_csv};
  for (std::size_t a = 0; a < outputs.size(); ++a) {
    if (outputs[a].empty())
      continue;
    if (path_is_within(dataset_root, outputs[a])) {
      throw std::invalid_argument(
          "output paths must be outside the source dataset root");
    }
    if (comparable(outputs[a]) == comparable(args.ledger)) {
      throw std::invalid_argument("an output path aliases the input ledger");
    }
    if (comparable(outputs[a]) == comparable(args.cmf)) {
      throw std::invalid_argument("an output path aliases the input CMF table");
    }
    for (std::size_t b = a + 1; b < outputs.size(); ++b) {
      if (!outputs[b].empty() &&
          comparable(outputs[a]) == comparable(outputs[b])) {
        throw std::invalid_argument("output paths must be distinct");
      }
    }
  }
}

void write_double_array(JsonWriter &writer, const std::vector<double> &values) {
  writer.begin_array();
  for (const double value : values)
    writer.value(value);
  writer.end_array();
}

void write_optional(JsonWriter &writer, const std::optional<double> &value) {
  if (value) {
    writer.value(*value);
  } else {
    writer.null();
  }
}

void write_optional_array(JsonWriter &writer,
                          const std::optional<std::vector<double>> &values) {
  if (values) {
    write_double_array(writer, *values);
  } else {
    writer.null();
  }
}

std::string csv_escape(std::string_view value) {
  if (value.find_first_of(",\"\r\n") == std::string_view::npos)
    return std::string(value);
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const char character : value) {
    if (character == '"')
      escaped.push_back('"');
    escaped.push_back(character);
  }
  escaped.push_back('"');
  return escaped;
}

void write_json(std::ostream &out, const SpectroArchiveIngest &archive,
                const SpectroClosureResult &closure,
                std::string_view dataset_label, std::string_view ledger_label,
                std::string_view cmf_label, std::string_view cmf_sha256,
                bool aliases_verified) {
  JsonWriter writer(out);
  writer.begin_object();
  writer.key("schema_version");
  writer.value(kSpectroIngestSchemaVersion);
  writer.key("dataset");
  writer.value(dataset_label);
  writer.key("ledger");
  writer.begin_object();
  writer.key("file");
  writer.value(ledger_label);
  writer.key("sha256");
  writer.value(archive.ledger_sha256);
  writer.end_object();
  writer.key("evidence");
  writer.begin_object();
  writer.key("canonical_readings");
  writer.value(static_cast<std::int64_t>(archive.canonical_reading_count));
  writer.key("declared_aliases");
  writer.value(static_cast<std::int64_t>(archive.alias_count));
  writer.key("aliases_verified");
  writer.value(aliases_verified);
  writer.key("measurement_groups");
  writer.value(static_cast<std::int64_t>(archive.groups.size()));
  writer.end_object();
  writer.key("method");
  writer.begin_object();
  writer.key("sample_weighting");
  writer.value("uniform_equal_weight");
  writer.key("normalization");
  writer.value("each spectrum divided by its computed spectral integral");
  writer.key("variation_label");
  writer.value("within_group_observed_variation");
  writer.key("cause");
  writer.value("unresolved");
  writer.key("recorded_metadata");
  writer.value("XYZ, totalRadiance, CCT, and Duv are retained without "
               "inferring undocumented conventions");
  writer.end_object();
  writer.key("closure");
  writer.begin_object();
  writer.key("observer_file");
  writer.value(cmf_label);
  writer.key("observer_sha256");
  writer.value(cmf_sha256);
  writer.key("sample_weighting");
  writer.value(closure.sample_weighting);
  writer.key("scale_source");
  writer.value(closure.scale_source);
  writer.key("scale_value");
  writer.value(closure.scale_value);
  writer.key("max_absolute_relative_residual_percent");
  writer.value(closure.max_absolute_relative_residual_percent);
  writer.key("rms_relative_residual_percent");
  writer.value(closure.rms_relative_residual_percent);
  writer.key("interpretation");
  writer.value("numerical agreement with one fitted proportional scale; "
               "instrument software is not identified");
  writer.end_object();
  writer.key("groups");
  writer.begin_array();
  std::size_t closure_index = 0;
  for (const IngestedSpectroGroup &group : archive.groups) {
    const auto &analysis = group.analysis;
    writer.begin_object();
    writer.key("group_id");
    writer.value(group.group_id);
    writer.key("count");
    writer.value(static_cast<std::int64_t>(group.readings.size()));
    writer.key("wavelength_nm");
    write_double_array(writer, group.summary.wavelength_nm);
    writer.key("wavelength_step_nm");
    writer.value(analysis.wavelength_step_nm);
    writer.key("absolute");
    writer.begin_object();
    writer.key("mean_spectral_radiance");
    write_double_array(writer, group.summary.mean_spectral_radiance);
    writer.key("sample_stddev_spectral_radiance");
    write_optional_array(writer, group.summary.sample_stddev_spectral_radiance);
    writer.key("mean_spectral_integral");
    writer.value(analysis.mean_spectral_integral);
    writer.key("sample_stddev_spectral_integral");
    write_optional(writer, analysis.sample_stddev_spectral_integral);
    writer.key("coefficient_of_variation");
    write_optional(writer, analysis.coefficient_of_variation);
    writer.end_object();
    writer.key("shape");
    writer.begin_object();
    writer.key("mean_normalized_spectrum");
    write_double_array(writer, analysis.mean_normalized_spectrum);
    writer.key("sample_stddev_normalized_spectrum");
    write_optional_array(writer, analysis.sample_stddev_normalized_spectrum);
    writer.key("max_relative_l2");
    write_optional(writer, analysis.max_shape_relative_l2);
    writer.end_object();
    writer.key("recorded_xyz_chromaticity");
    writer.begin_object();
    writer.key("max_pair_delta_u_prime_v_prime");
    write_optional(writer, analysis.max_pair_delta_u_prime_v_prime);
    writer.end_object();
    writer.key("singleton_reason");
    if (group.readings.size() == 1) {
      writer.value("one measurement does not establish variation");
    } else {
      writer.null();
    }
    writer.key("readings");
    writer.begin_array();
    for (std::size_t index = 0; index < group.readings.size(); ++index) {
      const auto &reading = group.readings[index];
      const auto &analyzed = analysis.readings[index];
      writer.begin_object();
      writer.key("measurement_index");
      writer.value(static_cast<std::int64_t>(reading.identity.repeat_index));
      writer.key("path");
      writer.value(reading.identity.canonical_path);
      writer.key("sha256");
      writer.value(reading.identity.sha256);
      writer.key("spectral_integral");
      writer.value(analyzed.spectral_integral);
      writer.key("recorded_xyz");
      writer.begin_array();
      for (const double value : reading.measurement.recorded_xyz) {
        writer.value(value);
      }
      writer.end_array();
      writer.key("recorded_total_radiance");
      writer.value(reading.measurement.recorded_total_radiance);
      writer.key("recorded_cct_k");
      writer.value(reading.measurement.recorded_cct_k);
      writer.key("recorded_duv");
      writer.value(reading.measurement.recorded_duv);
      writer.key("computed_xyz");
      writer.begin_array();
      for (const double value :
           closure.readings.at(closure_index).computed_xyz) {
        writer.value(value);
      }
      writer.end_array();
      writer.key("signed_relative_residual_percent");
      writer.begin_array();
      for (const double value : closure.readings.at(closure_index)
                                    .signed_relative_residual_percent) {
        writer.value(value);
      }
      writer.end_array();
      writer.key("chromaticity");
      writer.begin_object();
      writer.key("x");
      writer.value(analyzed.recorded_xyz_chromaticity.x);
      writer.key("y");
      writer.value(analyzed.recorded_xyz_chromaticity.y);
      writer.key("u_prime");
      writer.value(analyzed.recorded_xyz_chromaticity.u_prime);
      writer.key("v_prime");
      writer.value(analyzed.recorded_xyz_chromaticity.v_prime);
      writer.end_object();
      ++closure_index;
      writer.end_object();
    }
    writer.end_array();
    writer.end_object();
  }
  writer.end_array();
  writer.end_object();
}

void write_groups_csv(std::ostream &out, const SpectroArchiveIngest &archive) {
  out << std::setprecision(17)
      << "group_id,count,mean_spectral_integral,"
         "sample_stddev_spectral_integral,coefficient_of_variation,"
         "max_pair_delta_u_prime_v_prime,max_shape_relative_l2,"
         "variation_label\n";
  for (const auto &group : archive.groups) {
    const auto &analysis = group.analysis;
    out << csv_escape(group.group_id) << ',' << group.readings.size() << ','
        << analysis.mean_spectral_integral << ',';
    if (analysis.sample_stddev_spectral_integral) {
      out << *analysis.sample_stddev_spectral_integral;
    }
    out << ',';
    if (analysis.coefficient_of_variation) {
      out << *analysis.coefficient_of_variation;
    }
    out << ',';
    if (analysis.max_pair_delta_u_prime_v_prime)
      out << *analysis.max_pair_delta_u_prime_v_prime;
    out << ',';
    if (analysis.max_shape_relative_l2) {
      out << *analysis.max_shape_relative_l2;
    }
    out << ','
        << (group.readings.size() == 1
                ? "not_established_single_measurement"
                : "within_group_observed_variation")
        << '\n';
  }
}

void write_spectra_csv(std::ostream &out, const SpectroArchiveIngest &archive) {
  out << std::setprecision(17)
      << "group_id,wavelength_nm,mean_absolute_radiance,"
         "sample_stddev_absolute_radiance,mean_normalized_radiance,"
         "sample_stddev_normalized_radiance\n";
  for (const auto &group : archive.groups) {
    for (std::size_t index = 0; index < group.summary.wavelength_nm.size();
         ++index) {
      out << csv_escape(group.group_id) << ','
          << group.summary.wavelength_nm[index] << ','
          << group.summary.mean_spectral_radiance[index] << ',';
      if (group.summary.sample_stddev_spectral_radiance) {
        out << group.summary.sample_stddev_spectral_radiance->at(index);
      }
      out << ',' << group.analysis.mean_normalized_spectrum[index] << ',';
      if (group.analysis.sample_stddev_normalized_spectrum) {
        out << group.analysis.sample_stddev_normalized_spectrum->at(index);
      }
      out << '\n';
    }
  }
}

void write_readings_csv(std::ostream &out, const SpectroArchiveIngest &archive,
                        const SpectroClosureResult &closure) {
  out << std::setprecision(17)
      << "group_id,measurement_index,canonical_path,sha256,spectral_integral,"
         "wavelength_binary64_le_sha256,radiance_binary64_le_sha256,"
         "recorded_x,recorded_y,recorded_z,chromaticity_x,chromaticity_y,"
         "u_prime,v_prime,recorded_total_radiance,recorded_cct_k,recorded_duv,"
         "computed_x,computed_y,computed_z,residual_x_percent,"
         "residual_y_percent,residual_z_percent\n";
  std::size_t closure_index = 0;
  for (const auto &group : archive.groups) {
    for (std::size_t index = 0; index < group.readings.size(); ++index) {
      const auto &reading = group.readings[index];
      const auto &analyzed = group.analysis.readings[index];
      out << csv_escape(group.group_id) << ','
          << reading.identity.repeat_index << ','
          << csv_escape(reading.identity.canonical_path) << ','
          << reading.identity.sha256 << ',' << analyzed.spectral_integral << ','
          << binary64_le_sha256(reading.measurement.wavelength_nm) << ','
          << binary64_le_sha256(reading.measurement.spectral_radiance);
      for (const double value : reading.measurement.recorded_xyz) {
        out << ',' << value;
      }
      out << ',' << analyzed.recorded_xyz_chromaticity.x << ','
          << analyzed.recorded_xyz_chromaticity.y << ','
          << analyzed.recorded_xyz_chromaticity.u_prime << ','
          << analyzed.recorded_xyz_chromaticity.v_prime << ','
          << reading.measurement.recorded_total_radiance << ','
          << reading.measurement.recorded_cct_k << ','
          << reading.measurement.recorded_duv;
      const auto &closed = closure.readings.at(closure_index++);
      for (const double value : closed.computed_xyz)
        out << ',' << value;
      for (const double value : closed.signed_relative_residual_percent) {
        out << ',' << value;
      }
      out << '\n';
    }
  }
}

} // namespace

int cmd_spectro_ingest(int argc, char **argv) {
  if (argc == 1 && (std::string_view(argv[0]) == "-h" ||
                    std::string_view(argv[0]) == "--help")) {
    usage(std::cout);
    return 0;
  }
  const std::optional<Arguments> parsed = parse_arguments(argc, argv);
  if (!parsed) {
    usage(std::cerr);
    return 2;
  }
  const Arguments &args = *parsed;
  try {
    const std::optional<ResolvedDataset> dataset =
        resolve_dataset_root(args.root_or_id, args.config);
    if (!dataset) {
      std::cerr << "camera_iq spectro-ingest: dataset root or ID not found\n";
      return 1;
    }
    validate_output_paths(args, dataset->root);
    constexpr std::size_t kMaxMetadataInputBytes = 4u << 20;
    const std::string ledger =
        read_file(args.ledger, "identity ledger", kMaxMetadataInputBytes);
    const SpectroArchiveIngest archive =
        ingest_spectro_archive(dataset->root, ledger, args.verify_aliases);
    const std::string cmf_csv =
        read_file(args.cmf, "CMF table", kMaxMetadataInputBytes);
    const SpectroCmfTable observer = read_spectro_cmf_csv(cmf_csv);
    std::vector<SpectroMeasurement> measurements;
    measurements.reserve(archive.canonical_reading_count);
    for (const auto &group : archive.groups) {
      for (const auto &reading : group.readings) {
        measurements.push_back(reading.measurement);
      }
    }
    const SpectroClosureResult closure =
        compute_spectro_closure(measurements, observer);
    const std::string dataset_label = dataset_display_label(*dataset);
    const std::string ledger_label = args.ledger.filename().string();
    const std::string cmf_label = args.cmf.filename().string();
    const std::string cmf_sha256 = sha256_hex(cmf_csv);

    if (args.json_out.empty()) {
      write_json(std::cout, archive, closure, dataset_label, ledger_label,
                 cmf_label, cmf_sha256, args.verify_aliases);
      std::cout << '\n';
    } else if (!write_output_file_checked(
                   args.json_out, "spectro-ingest",
                   [&](std::ostream &out) {
                     write_json(out, archive, closure, dataset_label,
                                ledger_label, cmf_label, cmf_sha256,
                                args.verify_aliases);
                   },
                   std::cerr)) {
      return 1;
    }
    if (!args.groups_csv.empty() &&
        !write_output_file_checked(
            args.groups_csv, "spectro-ingest",
            [&](std::ostream &out) { write_groups_csv(out, archive); },
            std::cerr, false)) {
      return 1;
    }
    if (!args.spectra_csv.empty() &&
        !write_output_file_checked(
            args.spectra_csv, "spectro-ingest",
            [&](std::ostream &out) { write_spectra_csv(out, archive); },
            std::cerr, false)) {
      return 1;
    }
    if (!args.readings_csv.empty() && !write_output_file_checked(
                                          args.readings_csv, "spectro-ingest",
                                          [&](std::ostream &out) {
                                            write_readings_csv(out, archive,
                                                               closure);
                                          },
                                          std::cerr, false)) {
      return 1;
    }
    return 0;
  } catch (const std::invalid_argument &error) {
    std::cerr << "camera_iq spectro-ingest: " << error.what() << '\n';
    return 2;
  } catch (const std::exception &error) {
    std::cerr << "camera_iq spectro-ingest: " << error.what() << '\n';
    return 1;
  }
}

} // namespace camera_iq
