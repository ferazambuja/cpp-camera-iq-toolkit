#include "camera_iq/commands.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "harness.hpp"

using camera_iq::cmd_sfr;
using test::check;

namespace {

int run_sfr(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return cmd_sfr(static_cast<int>(argv.size()), argv.data());
}

std::pair<int, std::string> run_sfr_with_stderr(
    const std::vector<std::string>& args) {
  std::ostringstream captured;
  auto* original = std::cerr.rdbuf(captured.rdbuf());
  const int result = run_sfr(args);
  std::cerr.rdbuf(original);
  return {result, captured.str()};
}

}  // namespace

void TESTS() {
  check(run_sfr({}) == 2, "sfr command: dataset argument required");
  check(run_sfr({"missing_dataset", "--raw"}) == 2,
        "sfr command: raw requires a value");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF"}) == 2,
        "sfr command: ROI or oracle required");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "bad"}) == 2,
        "sfr command: malformed ROI rejected before dataset I/O");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "10,10,40,40"}) == 1,
        "sfr command: valid explicit ROI syntax reaches dataset resolution");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--oracle-y-multi",
                 "Results/edge__Y_multi.csv"}) == 1,
        "sfr command: valid oracle ROI syntax reaches dataset resolution");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--field-map"}) == 2,
        "sfr command: field-map requires oracle Y_multi");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--field-map",
                 "--oracle-y-multi", "Results/edge__Y_multi.csv"}) == 1,
        "sfr command: valid field-map syntax reaches dataset resolution");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--field-map",
                 "--oracle-y-multi", "Results/edge__Y_multi.csv",
                 "--edge-roi", "10,10,40,40"}) == 2,
        "sfr command: field-map rejects explicit edge ROI");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "10,10,40,40", "--near-saturation-fraction", "1.5"}) == 2,
        "sfr command: bad saturation fraction rejected");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "10,10,40,40", "--near-saturation-fraction", "nan"}) == 2,
        "sfr command: non-finite saturation fraction rejected");
  check(run_sfr({"--bogus", "--raw", "edge.NEF", "--edge-roi",
                 "10,10,40,40"}) == 2,
        "sfr command: unknown leading option rejected as an argument");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "10,10,40,40", "--oracle-y-multi", "oracle.csv"}) == 2,
        "sfr command: explicit and oracle ROI sources are mutually exclusive");

  {
    const auto root =
        std::filesystem::temp_directory_path() / "camera_iq_cmd_sfr_mismatch";
    std::filesystem::create_directories(root);
    {
      std::ofstream os(root / "oracle.csv");
      os << "File,expected.NEF\n"
            "Run date,fixture\n\n"
            "N,Distance %,Direction,X1,Y1,X2,Y2,Width,Height,Region,Edge ID\n"
            "1,0,AL,1,1,24,24,24,24,Center,0_0_R\n\n"
            "N,MTF50 (Cy/Pxl),R1090 (pxl),CA area(pxl),MTF50 (LW/PH),"
            "R1090 (/PH),Peak MTF,MTF50P (Cy/Pxl)\n"
            "1,0.2,2.0,0,100,100,1.0,0.2\n";
    }
    const auto [result, error] =
        run_sfr_with_stderr({root.string(), "--raw", "wrong.NEF",
                             "--oracle-y-multi", "oracle.csv"});
    check(result == 1 &&
              error.find("RAW filename does not match oracle File entry") !=
                  std::string::npos,
          "sfr command: center mode rejects a mismatched RAW/oracle pair");
    std::filesystem::remove_all(root);
  }

  {
    // A configured dataset stamps its id onto the emitted label, so an input
    // that leaves the root would be published as that dataset's evidence.
    // Directory mode claims no dataset, so it stays permissive.
    namespace fs = std::filesystem;
    const auto base = fs::temp_directory_path() / "camera_iq_sfr_contained";
    fs::remove_all(base);
    fs::create_directories(base / "root" / "Images");
    fs::create_directories(base / "outside");
    {
      std::ofstream os(base / "outside" / "secret.NEF");
      os << "outside evidence";
    }
    fs::create_symlink(base / "outside" / "secret.NEF",
                       base / "root" / "link.NEF");
    {
      std::ofstream os(base / "outside" / "oracle.csv");
      os << "outside oracle";
    }
    fs::create_symlink(base / "outside" / "oracle.csv",
                       base / "root" / "oracle-link.csv");
    const auto config = base / "datasets.json";
    {
      std::ofstream os(config);
      os << "{\"datasets\":{\"fixture\":{\"root\":\""
         << (base / "root").generic_string() << "\"}}}\n";
    }

    const std::string cfg = config.string();
    const auto absolute_raw = (base / "outside" / "secret.NEF").string();
    check(run_sfr({"fixture", "--config", cfg, "--raw", absolute_raw,
                   "--edge-roi", "0,0,64,64"}) == 2,
          "sfr command: configured dataset refuses an absolute RAW");
    check(run_sfr({"fixture", "--config", cfg, "--raw",
                   "Images/../../outside/secret.NEF", "--edge-roi",
                   "0,0,64,64"}) == 2,
          "sfr command: configured dataset refuses RAW parent traversal");
    check(run_sfr({"fixture", "--config", cfg, "--raw", "link.NEF",
                   "--edge-roi", "0,0,64,64"}) == 2,
          "sfr command: configured dataset refuses a RAW symlink escape");
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/edge.NEF",
                   "--oracle-y-multi", "../../outside/oracle.csv"}) == 2,
          "sfr command: configured dataset refuses oracle traversal");
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/edge.NEF",
                   "--oracle-y-multi",
                   (base / "outside" / "oracle.csv").string()}) == 2,
          "sfr command: configured dataset refuses an absolute oracle");
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/edge.NEF",
                   "--oracle-y-multi", "oracle-link.csv"}) == 2,
          "sfr command: configured dataset refuses an oracle symlink escape");

    const auto source = base / "root" / "Images" / "source.NEF";
    {
      std::ofstream os(source);
      os << "source evidence";
    }
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/source.NEF",
                   "--edge-roi", "0,0,64,64", "--out", source.string()}) == 2,
          "sfr command: output cannot alias the RAW input");
    const auto normalized_source =
        source.parent_path() / "." / source.filename();
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/source.NEF",
                   "--edge-roi", "0,0,64,64", "--out",
                   normalized_source.string()}) == 2,
          "sfr command: normalized-equivalent output cannot alias RAW");
    const auto hard_linked_output = base / "hard-linked-sfr-output.json";
    fs::create_hard_link(source, hard_linked_output);
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/source.NEF",
                   "--edge-roi", "0,0,64,64", "--out",
                   hard_linked_output.string()}) == 2,
          "sfr command: hard-linked output cannot alias RAW");
    const auto contained_oracle = base / "root" / "oracle-input.csv";
    {
      std::ofstream os(contained_oracle);
      os << "oracle evidence";
    }
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/source.NEF",
                   "--oracle-y-multi", "oracle-input.csv", "--out",
                   contained_oracle.string()}) == 2,
          "sfr command: output cannot alias oracle input");
    const std::string config_before = [&] {
      std::ifstream input(config, std::ios::binary);
      return std::string(std::istreambuf_iterator<char>(input),
                         std::istreambuf_iterator<char>());
    }();
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/source.NEF",
                   "--edge-roi", "0,0,64,64", "--out", cfg}) == 2,
          "sfr command: output cannot alias dataset configuration");
    std::ifstream preserved_config(config, std::ios::binary);
    check(std::string(std::istreambuf_iterator<char>(preserved_config),
                      std::istreambuf_iterator<char>()) == config_before,
          "sfr command: config alias refusal preserves configuration bytes");
    std::ifstream preserved(source);
    check(std::string(std::istreambuf_iterator<char>(preserved),
                      std::istreambuf_iterator<char>()) == "source evidence",
          "sfr command: alias refusal preserves RAW bytes");

    // A contained relative input passes containment and fails later, at I/O.
    check(run_sfr({"fixture", "--config", cfg, "--raw", "Images/edge.NEF",
                   "--edge-roi", "0,0,64,64"}) == 1,
          "sfr command: contained relative RAW reaches I/O");
    // Directory mode attributes nothing to a dataset, so it still resolves.
    check(run_sfr({(base / "root").string(), "--raw", absolute_raw,
                   "--edge-roi", "0,0,64,64"}) == 1,
          "sfr command: directory mode still accepts an absolute RAW");
    fs::remove_all(base);
  }
}
