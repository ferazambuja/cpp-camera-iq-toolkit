#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "camera_iq/spectro_analysis.hpp"
#include "camera_iq/spectro_ledger.hpp"
#include "camera_iq/spectro_measurement.hpp"

namespace camera_iq {

// Authoritative schema written by the spectro-ingest JSON serializer.
inline constexpr int kSpectroIngestSchemaVersion = 2;

struct IngestedSpectroReading {
  SpectroLedgerEntry identity;
  SpectroMeasurement measurement;
};

struct IngestedSpectroGroup {
  std::string group_id;
  std::vector<IngestedSpectroReading> readings;
  SpectroGroupSummary summary;
  SpectroGroupAnalysis analysis;
};

struct SpectroArchiveIngest {
  std::string ledger_sha256;
  std::size_t canonical_reading_count = 0;
  std::size_t alias_count = 0;
  std::vector<IngestedSpectroGroup> groups;
};

// Loads every canonical reading declared by `ledger_csv` from `archive_root`.
// Each file is read once; SHA-256 is verified on that byte buffer before the
// same buffer is passed to the MAT parser. Canonical paths and, when requested,
// aliases must resolve to regular non-symlink files below the canonical root.
// `max_input_bytes` bounds each compressed MAT file before it is retained in
// memory; the MAT reader separately bounds cumulative inflated payload bytes.
SpectroArchiveIngest
ingest_spectro_archive(const std::filesystem::path &archive_root,
                       const std::string &ledger_csv,
                       bool verify_aliases = false,
                       std::size_t max_input_bytes = 64u << 20);

} // namespace camera_iq
