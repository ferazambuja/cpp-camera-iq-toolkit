#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "camera_iq/spectro_analysis.hpp"
#include "camera_iq/spectro_ledger.hpp"
#include "camera_iq/spectro_measurement.hpp"

namespace camera_iq {

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
SpectroArchiveIngest
ingest_spectro_archive(const std::filesystem::path &archive_root,
                       const std::string &ledger_csv,
                       bool verify_aliases = false);

} // namespace camera_iq
