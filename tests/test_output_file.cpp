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
    const auto input = root / "same-input.raw";
    {
      std::ofstream seed(input, std::ios::binary);
      seed << "source evidence";
    }
    test::check(output_path_aliases_input(input, input),
                "output alias check catches identical input path");
    test::check(output_path_aliases_input(input.parent_path() / "." /
                                              input.filename(),
                                          input),
                "output alias check catches normalized equivalent path");
    test::check(!output_path_aliases_input(root / "different.json", input),
                "output alias check permits distinct path");
    std::ostringstream err;
    test::check(reject_output_aliasing_inputs(
                    input, {std::nullopt, std::filesystem::path{}, input},
                    "fixture", err),
                "multi-input alias guard skips absent inputs and catches the source");
    test::check(err.str().find(input.string()) != std::string::npos,
                "multi-input alias guard names the protected input");
  }

  {
    const auto scanned = root / "scanned-inputs";
    std::filesystem::create_directories(scanned);
    std::ostringstream inside_error;
    test::check(reject_output_within_input_directory(
                    scanned / "nested" / "report.json", scanned, "fixture",
                    inside_error),
                "directory guard catches a not-yet-created output below the input root");
    test::check(inside_error.str().find("scanned input directory") !=
                    std::string::npos,
                "directory guard explains the scanned-directory boundary");
    std::ostringstream sibling_error;
    test::check(!reject_output_within_input_directory(
                    root / "scanned-inputs-sibling" / "report.json", scanned,
                    "fixture", sibling_error),
                "directory guard does not confuse a prefix-sharing sibling with a child");
  }

  {
    const auto source = root / "source.mat";
    const auto output = root / "hard-linked-output.json";
    {
      std::ofstream seed(source, std::ios::binary);
      seed << "source evidence";
    }
    std::filesystem::create_hard_link(source, output);
    test::check(output_path_aliases_input(output, source),
                "output alias check catches hard-linked input identity");
    std::ostringstream err;
    const bool ok = write_output_file_checked(
        output, "fixture", [](std::ostream& os) { os << "replacement"; },
        err);
    test::check(!ok, "checked writer refuses a multiply-linked output file");
    test::check(read_text(source) == "source evidence",
                "checked writer preserves a hard-linked source file");
  }

  {
    const auto source = root / "symlink-source.mat";
    const auto output = root / "symlinked-output.json";
    {
      std::ofstream seed(source, std::ios::binary);
      seed << "source evidence";
    }
    std::filesystem::create_symlink(source, output);
    std::ostringstream err;
    const bool ok = write_output_file_checked(
        output, "fixture", [](std::ostream& os) { os << "replacement"; },
        err);
    test::check(!ok, "checked writer refuses a symlinked output file");
    test::check(read_text(source) == "source evidence",
                "checked writer preserves a symlink target");
  }

  {
    const auto real_parent = root / "real-output-parent";
    const auto linked_parent = root / "linked-output-parent";
    std::filesystem::create_directories(real_parent);
    std::filesystem::create_directory_symlink(real_parent, linked_parent);
    const auto output = linked_parent / "redirected.json";
    std::ostringstream err;
    const bool ok = write_output_file_checked(
        output, "fixture", [](std::ostream& os) { os << "replacement"; },
        err);
    test::check(!ok, "checked writer refuses a symlinked output parent");
    test::check(!std::filesystem::exists(real_parent / "redirected.json"),
                "checked writer does not follow a symlinked output parent");
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
