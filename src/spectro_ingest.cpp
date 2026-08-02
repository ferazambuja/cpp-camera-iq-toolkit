#include "camera_iq/spectro_ingest.hpp"

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "camera_iq/mat_file.hpp"
#include "camera_iq/sha256.hpp"

namespace camera_iq {
namespace {

[[noreturn]] void refuse(const std::string &what) {
  throw std::runtime_error("spectro ingest: " + what);
}

bool stays_below(const std::filesystem::path &root,
                 const std::filesystem::path &resolved) {
  const std::filesystem::path relative = resolved.lexically_relative(root);
  if (relative.empty() || relative.is_absolute())
    return false;
  for (const auto &component : relative) {
    if (component == "..")
      return false;
  }
  return true;
}

std::filesystem::path
resolve_regular_file(const std::filesystem::path &canonical_root,
                     const std::filesystem::path &relative) {
  std::filesystem::path current = canonical_root;
  std::error_code error;
  for (const auto &component : relative) {
    current /= component;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(current, error);
    if (error) {
      refuse("cannot inspect declared path " + relative.generic_string());
    }
    if (std::filesystem::is_symlink(status)) {
      refuse("declared path uses a symlink: " + relative.generic_string());
    }
  }
  if (!std::filesystem::is_regular_file(current, error) || error) {
    refuse("declared path is not a regular file: " + relative.generic_string());
  }
  const std::filesystem::path resolved =
      std::filesystem::canonical(current, error);
  if (error || !stays_below(canonical_root, resolved)) {
    refuse("declared path resolves outside the archive root: " +
           relative.generic_string());
  }
  return resolved;
}

std::string read_bytes(const std::filesystem::path &path,
                       const std::filesystem::path &shown) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    refuse("cannot open declared path " + shown.generic_string());
  }
  std::string bytes{std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>()};
  if (input.bad()) {
    refuse("cannot read declared path " + shown.generic_string());
  }
  return bytes;
}

} // namespace

SpectroArchiveIngest
ingest_spectro_archive(const std::filesystem::path &archive_root,
                       const std::string &ledger_csv, bool verify_aliases) {
  std::error_code error;
  const std::filesystem::file_status root_status =
      std::filesystem::symlink_status(archive_root, error);
  if (error || std::filesystem::is_symlink(root_status) ||
      !std::filesystem::is_directory(root_status)) {
    refuse("archive root must be a non-symlink directory");
  }
  const std::filesystem::path canonical_root =
      std::filesystem::canonical(archive_root, error);
  if (error)
    refuse("cannot canonicalize the archive root");

  const std::vector<SpectroLedgerGroup> declared =
      read_spectro_ledger(ledger_csv);
  SpectroArchiveIngest result;
  result.ledger_sha256 = sha256_hex(ledger_csv);
  result.groups.reserve(declared.size());

  for (const SpectroLedgerGroup &declared_group : declared) {
    IngestedSpectroGroup group;
    group.group_id = declared_group.group_id;
    group.readings.reserve(declared_group.readings.size());
    std::vector<SpectroMeasurement> measurements;
    measurements.reserve(declared_group.readings.size());

    for (const SpectroLedgerEntry &entry : declared_group.readings) {
      const std::filesystem::path relative(entry.canonical_path);
      const std::filesystem::path file =
          resolve_regular_file(canonical_root, relative);
      const std::string bytes = read_bytes(file, relative);
      const std::string digest = sha256_hex(bytes);
      if (digest != entry.sha256) {
        refuse("SHA-256 mismatch for " + relative.generic_string());
      }

      if (verify_aliases) {
        for (const std::string &alias_name : entry.alias_paths) {
          const std::filesystem::path alias_relative(alias_name);
          const std::filesystem::path alias =
              resolve_regular_file(canonical_root, alias_relative);
          const std::string alias_bytes = read_bytes(alias, alias_relative);
          if (sha256_hex(alias_bytes) != digest) {
            refuse("declared alias is not byte-identical: " +
                   alias_relative.generic_string());
          }
        }
      }

      SpectroMeasurement measurement =
          spectro_measurement_from_mat(read_mat_struct(bytes, "measurements"));
      measurements.push_back(measurement);
      group.readings.push_back(
          IngestedSpectroReading{entry, std::move(measurement)});
      ++result.canonical_reading_count;
      result.alias_count += entry.alias_paths.size();
    }
    group.summary = summarize_spectro_group(measurements);
    group.analysis = analyze_spectro_group(measurements);
    result.groups.push_back(std::move(group));
  }
  return result;
}

} // namespace camera_iq
