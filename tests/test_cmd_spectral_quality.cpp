#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "camera_iq/commands.hpp"
#include "harness.hpp"

using test::check;

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& p, const std::string& text) {
  std::ofstream os(p, std::ios::binary);
  os << text;
}

int run(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
  return camera_iq::cmd_spectral_quality(static_cast<int>(argv.size()),
                                         argv.data());
}

std::string read(const fs::path& p) {
  std::ifstream is(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(is)),
                     std::istreambuf_iterator<char>());
}

}  // namespace

void TESTS() {
  const fs::path root = fs::temp_directory_path() / "camera_iq_quality_cmd";
  fs::remove_all(root);
  fs::create_directories(root);

  // SSF channels are three unit axes; CMF X/Y/Z equal to those axes -> the
  // camera perfectly spans the CMF space -> residual 0, quality index 1.
  const std::string axes =
      "Wavelength "
      "(nm),Red,Green,Blue\n500,1,0,0\n510,0,1,0\n520,0,0,1\n530,0,0,0\n"
      "540,0,0,0\n";
  const std::string cmf_axes =
      "Wavelength (nm),X,Y,Z\n500,1,0,0\n510,0,1,0\n520,0,0,1\n530,0,0,0\n"
      "540,0,0,0\n";
  write_file(root / "ssf.csv", axes);
  write_file(root / "cmf_perfect.csv", cmf_axes);

  const fs::path out = root / "q.json";
  const int rc = run({"--ssf-csv", (root / "ssf.csv").string(), "--cmf",
                      (root / "cmf_perfect.csv").string(), "--camera-model",
                      "Ideal", "--out", out.string()});
  check(rc == 0, "quality cmd: succeeds on a well-formed run");
  const std::string json = read(out);
  check(json.find("\"method\":\"cmf_linear_fit_residual_luther\"") !=
            std::string::npos,
        "quality cmd: emits the Luther method label");
  check(json.find("\"quality_index\":1") != std::string::npos,
        "quality cmd: an SSF that spans the CMFs scores quality index 1");

  // CMF Y on an axis the SSF cannot reach -> that fit residual is 1.
  const std::string cmf_gap =
      "Wavelength (nm),X,Y,Z\n500,1,0,0\n510,0,0,0\n520,0,0,1\n530,0,1,0\n"
      "540,0,0,0\n";
  write_file(root / "cmf_gap.csv", cmf_gap);
  const fs::path out2 = root / "q2.json";
  const int rc2 = run({"--ssf-csv", (root / "ssf.csv").string(), "--cmf",
                       (root / "cmf_gap.csv").string(), "--camera-model", "Gap",
                       "--out", out2.string()});
  check(rc2 == 0, "quality cmd: succeeds when a CMF channel is unreachable");
  const std::string json2 = read(out2);
  check(json2.find("\"y\":1") != std::string::npos,
        "quality cmd: unreachable CMF channel reports a residual of 1");

  // Non-overlapping wavelength grids are rejected.
  write_file(root / "ssf_far.csv",
             "Wavelength (nm),Red,Green,Blue\n900,1,0,0\n910,0,1,0\n920,0,0,1\n"
             "930,0,0,0\n940,0,0,0\n");
  const int rc3 = run({"--ssf-csv", (root / "ssf_far.csv").string(), "--cmf",
                       (root / "cmf_perfect.csv").string(), "--camera-model",
                       "NoOverlap", "--out", (root / "q3.json").string()});
  check(rc3 == 1, "quality cmd: rejects SSF/CMF with no shared wavelengths");

  const auto malformed_rejected = [&](const std::string& name,
                                      const std::string& csv) {
    const fs::path path = root / (name + ".csv");
    write_file(path, csv);
    return run({"--ssf-csv", path.string(), "--cmf",
                (root / "cmf_perfect.csv").string(), "--camera-model", name,
                "--out", (root / (name + ".json")).string()}) == 1;
  };
  check(malformed_rejected(
            "trailing-number-junk",
            "Wavelength (nm),Red,Green,Blue\n500,1junk,0,0\n510,0,1,0\n"
            "520,0,0,1\n530,0,0,0\n540,0,0,0\n"),
        "quality cmd: rejects trailing junk in numeric fields");
  check(malformed_rejected(
            "short-row",
            "Wavelength (nm),Red,Green,Blue\n500,1,0,0\n510,0,1,0\n"
            "520,0,0,1\n530,0,0,0\n540,0,0,0\n550,0,0\n"),
        "quality cmd: rejects rows with missing channels");
  check(malformed_rejected(
            "extra-field",
            "Wavelength (nm),Red,Green,Blue\n500,1,0,0,9\n510,0,1,0\n"
            "520,0,0,1\n530,0,0,0\n540,0,0,0\n"),
        "quality cmd: rejects rows with extra channels");
  check(malformed_rejected(
            "fractional-wavelength",
            "Wavelength (nm),Red,Green,Blue\n500.4,1,0,0\n510,0,1,0\n"
            "520,0,0,1\n530,0,0,0\n540,0,0,0\n"),
        "quality cmd: rejects fractional wavelengths instead of rounding");
  check(malformed_rejected(
            "duplicate-wavelength",
            "Wavelength (nm),Red,Green,Blue\n500,1,0,0\n500,2,0,0\n"
            "510,0,1,0\n520,0,0,1\n530,0,0,0\n540,0,0,0\n"),
        "quality cmd: rejects duplicate wavelengths instead of overwriting");
  check(malformed_rejected("missing-header",
                           "500,9,9,9\n510,1,0,0\n520,0,1,0\n530,0,0,1\n"
                           "540,0,0,0\n"),
        "quality cmd: rejects data whose first row is not a header");
  check(malformed_rejected(
            "reordered-header",
            "Wavelength (nm),Green,Red,Blue\n500,1,0,0\n510,0,1,0\n"
            "520,0,0,1\n530,0,0,0\n540,0,0,0\n"),
        "quality cmd: rejects SSF channels in the wrong semantic order");

  write_file(root / "ssf_whitespace.csv",
             " Wavelength (nm) , Red , Green , Blue \n500 , 1 , 0 , 0 \n"
             "510 , 0 , 1 , 0 \n520 , 0 , 0 , 1 \n530 , 0 , 0 , 0 \n"
             "540 , 0 , 0 , 0 \n");
  check(run({"--ssf-csv", (root / "ssf_whitespace.csv").string(), "--cmf",
             (root / "cmf_perfect.csv").string(), "--camera-model",
             "Whitespace", "--out", (root / "whitespace.json").string()}) == 0,
        "quality cmd: trims ordinary whitespace around CSV fields");

  fs::remove_all(root);
}
