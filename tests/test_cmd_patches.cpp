#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::cmd_patches;
using test::check;

namespace {

int run_patches(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return cmd_patches(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

void TESTS() {
  const std::string valid_corners = "0,0;242,0;242,172;0,172";

  check(run_patches({"missing.RAF", "--sg-corners", valid_corners}) == 1,
        "patches command: valid sg-corners accepted as coordinate source");
  check(run_patches({"missing.RAF", "--sg-corners", valid_corners,
                     "--coords", "coord.csv"}) == 2,
        "patches command: sg-corners mutually exclusive with coords");
  check(run_patches({"missing.RAF", "--sg-corners", valid_corners,
                     "--rawdigger-csv", "rawdigger.csv"}) == 2,
        "patches command: sg-corners mutually exclusive with RawDigger CSV");
  check(run_patches({"missing.RAF", "--coords", "coord.csv",
                     "--rawdigger-oracle-csv", "rawdigger.csv"}) == 2,
        "patches command: RawDigger oracle requires sg-corners");
  check(run_patches({"missing.RAF", "--sg-corners", valid_corners,
                     "--rawdigger-oracle-csv", "rawdigger.csv",
                     "--flat-field-raw", "flat.RAF"}) == 2,
        "patches command: RawDigger oracle rejects corrected extraction");
  check(run_patches({"missing.RAF", "--sg-corner-source", "manual"}) == 2,
        "patches command: sg-corner-source requires sg-corners");
  check(run_patches({"missing.RAF", "--sg-corners", "0,0;bad;242,172;0,172"}) ==
            2,
        "patches command: malformed sg-corners rejected before RAW I/O");
  check(run_patches({"missing.RAF", "--sg-corners"}) == 2,
        "patches command: sg-corners requires a value");

  {
    // The emitted label attributes the capture to the dataset, so an input that
    // traverses or symlinks out of the root would publish outside evidence
    // under that id.
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() / "camera_iq_patches_contained";
    fs::remove_all(base);
    fs::create_directories(base / "root" / "Images");
    fs::create_directories(base / "outside");
    {
      std::ofstream os(base / "outside" / "secret.RAF");
      os << "outside evidence";
    }
    fs::create_symlink(base / "outside" / "secret.RAF",
                       base / "root" / "link.RAF");
    {
      std::ofstream os(base / "outside" / "side.csv");
      os << "outside side input";
    }
    fs::create_symlink(base / "outside" / "side.csv",
                       base / "root" / "side-link.csv");
    const auto config = base / "datasets.json";
    {
      std::ofstream os(config);
      os << "{\"datasets\":{\"fixture\":{\"root\":\""
         << (base / "root").generic_string() << "\"}}}\n";
    }

    const std::string cfg = config.string();
    check(run_patches({"Images/../../outside/secret.RAF", "--sg-corners",
                       valid_corners, "--dataset", "fixture", "--config",
                       cfg}) == 2,
          "patches command: dataset raw cannot traverse above root");
    check(run_patches({"link.RAF", "--sg-corners", valid_corners, "--dataset",
                       "fixture", "--config", cfg}) == 2,
          "patches command: dataset raw cannot symlink outside root");
    check(run_patches({"Images/edge.RAF", "--coords", "../outside/side.csv",
                       "--dataset", "fixture", "--config", cfg}) == 2,
          "patches command: coordinate input cannot traverse outside root");
    check(run_patches({"Images/edge.RAF", "--coords", "side-link.csv",
                       "--dataset", "fixture", "--config", cfg}) == 2,
          "patches command: coordinate input cannot symlink outside root");
    check(run_patches({"Images/edge.RAF", "--rawdigger-csv",
                       "../outside/side.csv", "--dataset", "fixture",
                       "--config", cfg}) == 2,
          "patches command: RawDigger input cannot traverse outside root");
    check(run_patches({"Images/edge.RAF", "--sg-corners", valid_corners,
                       "--rawdigger-oracle-csv", "../outside/side.csv",
                       "--dataset", "fixture", "--config", cfg}) == 2,
          "patches command: RawDigger oracle cannot traverse outside root");
    check(run_patches({"Images/edge.RAF", "--sg-corners", valid_corners,
                       "--reference-rgb", "../outside/side.csv", "--dataset",
                       "fixture", "--config", cfg}) == 2,
          "patches command: reference RGB cannot traverse outside root");
    check(run_patches({"Images/edge.RAF", "--sg-corners", valid_corners,
                       "--flat-field-raw", "../outside/secret.RAF",
                       "--dataset", "fixture", "--config", cfg}) == 2,
          "patches command: flat-field RAW cannot traverse outside root");

    const auto source = base / "root" / "Images" / "source.RAF";
    {
      std::ofstream os(source);
      os << "source evidence";
    }
    check(run_patches({"Images/source.RAF", "--sg-corners", valid_corners,
                       "--dataset", "fixture", "--config", cfg, "--out",
                       source.string()}) == 2,
          "patches command: report output cannot alias the RAW input");
    const auto normalized_source =
        source.parent_path() / "." / source.filename();
    check(run_patches({"Images/source.RAF", "--sg-corners", valid_corners,
                       "--dataset", "fixture", "--config", cfg, "--out",
                       normalized_source.string()}) == 2,
          "patches command: normalized-equivalent output cannot alias RAW");
    const auto hard_linked_output = base / "hard-linked-patches-output.json";
    fs::create_hard_link(source, hard_linked_output);
    check(run_patches({"Images/source.RAF", "--sg-corners", valid_corners,
                       "--dataset", "fixture", "--config", cfg, "--out",
                       hard_linked_output.string()}) == 2,
          "patches command: hard-linked output cannot alias RAW");
    const auto contained_coords = base / "root" / "coords.csv";
    {
      std::ofstream os(contained_coords);
      os << "coordinate evidence";
    }
    check(run_patches({"Images/source.RAF", "--coords", "coords.csv",
                       "--dataset", "fixture", "--config", cfg, "--out",
                       contained_coords.string()}) == 2,
          "patches command: output cannot alias coordinate input");
    const auto shared_output = base / "shared-output.json";
    check(run_patches({"Images/edge.RAF", "--sg-corners", valid_corners,
                       "--dataset", "fixture", "--config", cfg,
                       "--rgb-csv-out", shared_output.string(), "--out",
                       shared_output.string()}) == 2,
          "patches command: CSV and JSON outputs must be distinct");
    // A contained relative input passes containment and fails later, at I/O.
    check(run_patches({"Images/edge.RAF", "--sg-corners", valid_corners,
                       "--dataset", "fixture", "--config", cfg}) == 1,
          "patches command: contained relative raw reaches I/O");
    fs::remove_all(base);
  }
}
