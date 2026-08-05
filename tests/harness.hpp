#pragma once

// Minimal dependency-free test harness shared by all test executables.
// Each test .cpp defines TESTS() and includes this header, which supplies main().

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace test {

namespace detail {

class DocEvidenceRunState {
 public:
  explicit DocEvidenceRunState(const char* raw_expectations) {
    if (raw_expectations == nullptr) {
      return;
    }
    evidence_enabled_ = true;
    const std::string specification(raw_expectations);
    std::size_t begin = 0;
    while (begin < specification.size()) {
      const std::size_t end = specification.find(',', begin);
      const std::string item = specification.substr(begin, end - begin);
      const std::size_t equals = item.find('=');
      bool valid = equals != std::string::npos && equals != 0 &&
                   equals + 1 < item.size();
      std::size_t count = 0;
      if (valid) {
        for (std::size_t index = equals + 1; index < item.size(); ++index) {
          const char digit = item[index];
          if (digit < '0' || digit > '9') {
            valid = false;
            break;
          }
          count = count * 10 + static_cast<std::size_t>(digit - '0');
        }
        valid = valid && count > 0;
      }
      const std::string evidence_id = item.substr(0, equals);
      if (valid && expected_doc_evidence_.contains(evidence_id)) {
        valid = false;
      }
      if (valid) {
        expected_doc_evidence_.emplace(evidence_id, count);
      } else {
        fail("invalid documentation-evidence expectation: " + item);
      }
      if (end == std::string::npos) {
        break;
      }
      begin = end + 1;
    }
  }

  void fail(const std::string& name) {
    std::cout << "[fail] " << name << "\n";
    ++failures_;
  }

  void record(const char* evidence_id) {
    if (evidence_enabled_) {
      ++observed_doc_evidence_[evidence_id];
    }
  }

  void verify() {
    if (!evidence_enabled_) {
      return;
    }
    for (const auto& [evidence_id, expected] : expected_doc_evidence_) {
      const auto found = observed_doc_evidence_.find(evidence_id);
      const std::size_t observed =
          found == observed_doc_evidence_.end() ? 0 : found->second;
      if (observed != expected) {
        fail("documentation evidence " + evidence_id + ": expected " +
             std::to_string(expected) + " execution" +
             (expected == 1 ? "" : "s") + ", observed " +
             std::to_string(observed));
      } else {
        std::cout << "[ ok ] documentation evidence " << evidence_id
                  << ": expected " << expected << " execution"
                  << (expected == 1 ? "" : "s") << ", observed "
                  << observed << "\n";
      }
    }
    for (const auto& [evidence_id, observed] : observed_doc_evidence_) {
      if (!expected_doc_evidence_.contains(evidence_id)) {
        fail("unexpected documentation evidence " + evidence_id +
             ": observed " + std::to_string(observed));
      }
    }
  }

  [[nodiscard]] int failures() const { return failures_; }

 private:
  int failures_ = 0;
  bool evidence_enabled_ = false;
  std::map<std::string, std::size_t> expected_doc_evidence_;
  std::map<std::string, std::size_t> observed_doc_evidence_;
};

inline DocEvidenceRunState* active_doc_evidence_run_state = nullptr;

inline DocEvidenceRunState& current_doc_evidence_run() {
  if (active_doc_evidence_run_state == nullptr) {
    throw std::logic_error("test assertion used outside test::run");
  }
  return *active_doc_evidence_run_state;
}

}  // namespace detail

inline void check(bool condition, const std::string& name) {
  if (condition) {
    std::cout << "[ ok ] " << name << "\n";
  } else {
    detail::current_doc_evidence_run().fail(name);
  }
}

inline void check_near(double actual, double expected, double tol,
                       const std::string& name) {
  const bool ok = std::abs(actual - expected) <= tol;
  if (!ok) {
    std::cout << std::setprecision(17) << "       expected " << expected
              << ", got " << actual << ", |difference| "
              << std::abs(actual - expected) << ", tolerance " << tol
              << "\n";
  }
  check(ok, name);
}

inline void record_doc_evidence(const char* evidence_id) {
  detail::current_doc_evidence_run().record(evidence_id);
}

template <typename TestBody>
int run(TestBody body, const char* raw_expectations = nullptr) {
  if (detail::active_doc_evidence_run_state != nullptr) {
    check(false, "nested test::run is not supported");
    return 1;
  }
  detail::DocEvidenceRunState state(raw_expectations);
  detail::active_doc_evidence_run_state = &state;
  try {
    body();
  } catch (const std::exception& error) {
    check(false, std::string("uncaught exception: ") + error.what());
  } catch (...) {
    check(false, "uncaught non-standard exception");
  }
  state.verify();
  detail::active_doc_evidence_run_state = nullptr;
  std::cout << (state.failures() == 0 ? "all tests passed\n" : "TESTS FAILED\n");
  return state.failures() == 0 ? 0 : 1;
}

}  // namespace test

// Bind a public documentation claim to the assertion that exercises it. Under
// CTest, the target carries the exact expected execution counts. Recording the
// hit only after the assertion returns normally makes dead, skipped, or
// interrupted wrappers visible.
#define CAMERA_IQ_DOC_EVIDENCE(evidence_id, assertion) \
  ((assertion), ::test::record_doc_evidence(#evidence_id))

void TESTS();

#ifndef CAMERA_IQ_TEST_HARNESS_NO_MAIN
int main(int argc, char* argv[]) {
  const bool has_evidence_expectation =
      argc == 3 &&
      std::string(argv[1]) == "--camera-iq-doc-evidence-expect" &&
      argv[2][0] != '\0';
  if (argc != 1 && !has_evidence_expectation) {
    std::cerr << "[fail] test harness: expected no arguments or "
                 "--camera-iq-doc-evidence-expect <id=count,...>\n";
    return 1;
  }
  return test::run([] { TESTS(); },
                   has_evidence_expectation ? argv[2] : nullptr);
}
#endif
