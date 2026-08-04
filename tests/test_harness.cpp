#define CAMERA_IQ_TEST_HARNESS_NO_MAIN
#include "harness.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

bool contains(const std::string& text, const std::string& expected) {
  return text.find(expected) != std::string::npos;
}

struct RunResult {
  int exit_code = 0;
  std::string output;
};

template <typename TestBody>
RunResult run_and_capture(TestBody body) {
  std::ostringstream captured;
  std::streambuf* original = std::cout.rdbuf(captured.rdbuf());
  const int exit_code = test::run(body);
  std::cout.rdbuf(original);
  return {exit_code, captured.str()};
}

}  // namespace

int main() {
  const RunResult standard = run_and_capture([] {
    test::check(false, "failure recorded before throw");
    throw std::runtime_error("sentinel exception");
  });
  const RunResult non_standard = run_and_capture([] { throw 17; });
  // Run a clean body last, after two failing ones. Other executables that use
  // this harness depend on this path reporting success, and placing it here
  // also pins the per-run failure reset: without it the earlier failures would
  // leak forward and this body would report a failure it never had.
  const RunResult clean = run_and_capture([] {
    test::check(true, "passing check");
  });
  int failures = 0;
  const auto require = [&](bool condition, const char* name) {
    if (!condition) {
      std::cerr << "[fail] " << name << "\n";
      ++failures;
    }
  };
  require(standard.exit_code == 1,
          "harness: escaping standard exception returns failure");
  require(contains(standard.output, "[fail] failure recorded before throw"),
          "harness: retains failures recorded before exception");
  require(contains(standard.output,
                   "[fail] uncaught exception: sentinel exception"),
          "harness: reports the exception message");
  require(contains(standard.output, "TESTS FAILED"),
          "harness: emits the failure summary");
  require(non_standard.exit_code == 1,
          "harness: escaping non-standard exception returns failure");
  require(contains(non_standard.output,
                   "[fail] uncaught non-standard exception"),
          "harness: identifies a non-standard exception");
  require(contains(non_standard.output, "TESTS FAILED"),
          "harness: summarizes a non-standard exception");
  require(clean.exit_code == 0,
          "harness: a clean body returns success after earlier failed runs");
  require(contains(clean.output, "all tests passed"),
          "harness: emits the success summary");
  require(!contains(clean.output, "TESTS FAILED"),
          "harness: does not carry an earlier run's failures forward");

  return failures == 0 ? 0 : 1;
}
