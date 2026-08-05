#define CAMERA_IQ_TEST_HARNESS_NO_MAIN
#include "harness.hpp"

#include <cstdlib>
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

  setenv("CAMERA_IQ_DOC_EVIDENCE_EXPECT", "harness_runtime_probe=1", 1);
  const RunResult evidence_reached = run_and_capture([] {
    test::record_doc_evidence("harness_runtime_probe");
    test::check(true, "runtime evidence assertion reached");
  });
  const RunResult evidence_in_false_branch = run_and_capture([] {
    volatile bool execute = false;
    if (execute) {
      test::record_doc_evidence("harness_runtime_probe");
      test::check(true, "unreachable false-branch assertion");
    }
  });
  const RunResult evidence_after_return = run_and_capture([] {
    volatile bool stop = true;
    if (stop) {
      return;
    }
    test::record_doc_evidence("harness_runtime_probe");
    test::check(true, "unreachable post-return assertion");
  });
  const RunResult evidence_after_caught_throw = run_and_capture([] {
    try {
      throw std::runtime_error("assertion argument failed");
      test::record_doc_evidence("harness_runtime_probe");
    } catch (const std::runtime_error&) {
    }
  });
  unsetenv("CAMERA_IQ_DOC_EVIDENCE_EXPECT");
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
  require(evidence_reached.exit_code == 0,
          "harness: reached documentation evidence satisfies its runtime count");
  require(evidence_in_false_branch.exit_code == 1,
          "harness: evidence under a false branch is not counted as executed");
  require(contains(evidence_in_false_branch.output,
                   "expected 1 execution, observed 0"),
          "harness: false-branch evidence reports the missing execution");
  require(evidence_after_return.exit_code == 1,
          "harness: evidence after an early return is not counted as executed");
  require(contains(evidence_after_return.output,
                   "expected 1 execution, observed 0"),
          "harness: post-return evidence reports the missing execution");
  require(evidence_after_caught_throw.exit_code == 1,
          "harness: locally caught assertion failure is not counted as evidence");
  require(contains(evidence_after_caught_throw.output,
                   "expected 1 execution, observed 0"),
          "harness: caught assertion failure reports the missing execution");

  return failures == 0 ? 0 : 1;
}
