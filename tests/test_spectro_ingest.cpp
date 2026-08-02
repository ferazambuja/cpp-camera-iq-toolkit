#include "camera_iq/spectro_ingest.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "camera_iq/sha256.hpp"
#include "harness.hpp"
#include "spectro_mat_fixture.hpp"

using camera_iq::ingest_spectro_archive;
using camera_iq::sha256_hex;
using test::check;

namespace {

struct TempTree {
  TempTree() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root = std::filesystem::temp_directory_path() /
           ("camera_iq_spectro_ingest_" + std::to_string(stamp));
    std::filesystem::create_directories(root);
  }
  ~TempTree() { std::filesystem::remove_all(root); }
  std::filesystem::path root;
};

void write_bytes(const std::filesystem::path &path, const std::string &bytes) {
  std::ofstream output(path, std::ios::binary);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string ledger_row(const std::string &digest,
                       const std::string &canonical = "reading.mat",
                       const std::string &aliases = "") {
  return "group_id,repeat_index,canonical_path,sha256,alias_paths\n"
         "scene_01,1," +
         canonical + "," + digest + "," + aliases + "\n";
}

bool ingest_throws(const std::filesystem::path &root, const std::string &ledger,
                   bool verify_aliases = false,
                   std::size_t max_input_bytes = 64u << 20) {
  try {
    (void)ingest_spectro_archive(root, ledger, verify_aliases,
                                 max_input_bytes);
    return false;
  } catch (const std::runtime_error &) {
    return true;
  }
}

} // namespace

void TESTS() {
  TempTree tree;
  const std::string mat = spectro_test::measurement_mat();
  write_bytes(tree.root / "reading.mat", mat);
  const std::string ledger = ledger_row(sha256_hex(mat));

  const auto ingested = ingest_spectro_archive(tree.root, ledger, false);
  check(ingested.canonical_reading_count == 1 && ingested.alias_count == 0,
        "spectro ingest: counts distinguish readings from aliases");
  check(ingested.groups.size() == 1 && ingested.groups[0].readings.size() == 1,
        "spectro ingest: ledger groups connect to parsed readings");
  check(ingested.groups[0].readings[0].measurement.spectral_radiance ==
            std::vector<double>({1.0, 2.0, 3.0}),
        "spectro ingest: the verified bytes are parsed as a measurement");
  check(ingested.groups[0].analysis.count == 1 &&
            ingested.groups[0].analysis.mean_spectral_integral == 12.0,
        "spectro ingest: the executable chain includes group analysis");
  check(ingested.ledger_sha256 == sha256_hex(ledger),
        "spectro ingest: the exact ledger text has an inspectable identity");

  std::string wrong_digest = sha256_hex(mat);
  wrong_digest.back() = wrong_digest.back() == '0' ? '1' : '0';
  check(ingest_throws(tree.root, ledger_row(wrong_digest)),
        "spectro ingest: a canonical digest mismatch is refused");
  check(ingest_throws(tree.root, ledger, false, mat.size() - 1),
        "spectro ingest: an input larger than the byte limit is refused before "
        "parsing");

  std::filesystem::create_symlink(tree.root / "reading.mat",
                                  tree.root / "linked.mat");
  check(ingest_throws(tree.root, ledger_row(sha256_hex(mat), "linked.mat")),
        "spectro ingest: a symlinked canonical reading is refused");

  write_bytes(tree.root / "alias.mat", spectro_test::measurement_mat(1.0));
  const std::string with_alias =
      ledger_row(sha256_hex(mat), "reading.mat", "alias.mat");
  check(!ingest_throws(tree.root, with_alias, false),
        "spectro ingest: alias bytes are optional evidence");
  check(ingest_throws(tree.root, with_alias, true),
        "spectro ingest: a declared alias with different bytes is refused");
}
