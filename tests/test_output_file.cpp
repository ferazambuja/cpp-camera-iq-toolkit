#include "camera_iq/output_file.hpp"

#include "harness.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace camera_iq;

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  std::ostringstream ss;
  ss << is.rdbuf();
  return ss.str();
}

}  // namespace

void TESTS() {
  const auto root = std::filesystem::temp_directory_path() /
                    "camera_iq_test_output_file";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  {
    std::ostringstream err;
    const auto path = root / "nested" / "ok.json";
    const bool ok = write_output_file_checked(
        path, "fixture", [](std::ostream& os) { os << "{\"ok\":true}"; },
        err);
    test::check(ok, "checked writer succeeds");
    test::check(read_text(path) == "{\"ok\":true}\n",
                "checked writer appends newline");
    test::check(err.str().empty(), "checked writer keeps stderr empty");
  }

  {
    const auto path = root / "partial.json";
    {
      std::ofstream seed(path, std::ios::binary);
      seed << "partial";
    }
    std::ostringstream err;
    std::ostringstream bad;
    bad.setstate(std::ios::badbit);
    const bool ok = finish_output_stream_checked(
        bad, path, "fixture", err, /*remove_partial=*/true);
    test::check(!ok, "checked finish rejects bad stream");
    test::check(!std::filesystem::exists(path),
                "checked finish removes partial file");
    test::check(err.str().find("failed writing") != std::string::npos,
                "checked finish reports write failure");
  }

  {
    std::ostringstream err;
    const auto path = root / "rows.csv";
    const bool ok = write_output_file_checked(
        path, "fixture", [](std::ostream& os) { os << "a,b\n1,2\n"; }, err,
        /*append_newline=*/false);
    test::check(ok, "checked writer can preserve CSV trailing newline");
    test::check(read_text(path) == "a,b\n1,2\n",
                "checked writer does not add CSV blank row");
  }

  {
    std::ostringstream err;
    const auto path = root / "not_a_file";
    std::filesystem::create_directories(path);
    const bool ok = write_output_file_checked(
        path, "fixture", [](std::ostream& os) { os << "x"; }, err);
    test::check(!ok, "checked writer rejects unwritable path");
    test::check(err.str().find("cannot write") != std::string::npos,
                "checked writer reports open failure");
  }

  {
    std::ostringstream err;
    const auto path = root / "thrown.json";
    bool threw = false;
    try {
      write_output_file_checked(
          path, "fixture",
          [](std::ostream& os) {
            os << "{\"partial\":";
            throw std::runtime_error("serialization failed");
          },
          err);
    } catch (const std::runtime_error&) {
      threw = true;
    }
    test::check(threw, "checked writer rethrows write-body exceptions");
    test::check(!std::filesystem::exists(path),
                "checked writer removes partial file when the body throws");
  }

  std::filesystem::remove_all(root);
}
