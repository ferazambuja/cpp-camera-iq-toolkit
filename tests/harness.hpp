#pragma once

// Minimal dependency-free test harness shared by all test executables.
// Each test .cpp defines TESTS() and includes this header, which supplies main().

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>

namespace test {

inline int failures = 0;
inline bool doc_evidence_enabled = false;
inline std::map<std::string, std::size_t> expected_doc_evidence;
inline std::map<std::string, std::size_t> observed_doc_evidence;

inline void check(bool condition, const std::string& name) {
  if (condition) {
    std::cout << "[ ok ] " << name << "\n";
  } else {
    std::cout << "[fail] " << name << "\n";
    ++failures;
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

inline void configure_doc_evidence() {
  doc_evidence_enabled = false;
  expected_doc_evidence.clear();
  observed_doc_evidence.clear();

  const char* raw = std::getenv("CAMERA_IQ_DOC_EVIDENCE_EXPECT");
  if (raw == nullptr) {
    return;
  }
  doc_evidence_enabled = true;
  const std::string specification(raw);
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
    if (valid && expected_doc_evidence.contains(evidence_id)) {
      valid = false;
    }
    if (valid) {
      expected_doc_evidence.emplace(evidence_id, count);
    } else {
      check(false, "invalid documentation-evidence expectation: " + item);
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
}

inline void record_doc_evidence(const char* evidence_id) {
  if (doc_evidence_enabled) {
    ++observed_doc_evidence[evidence_id];
  }
}

inline void verify_doc_evidence() {
  if (!doc_evidence_enabled) {
    return;
  }
  for (const auto& [evidence_id, expected] : expected_doc_evidence) {
    const auto found = observed_doc_evidence.find(evidence_id);
    const std::size_t observed =
        found == observed_doc_evidence.end() ? 0 : found->second;
    check(observed == expected,
          "documentation evidence " + evidence_id + ": expected " +
              std::to_string(expected) + " execution" +
              (expected == 1 ? "" : "s") + ", observed " +
              std::to_string(observed));
  }
  for (const auto& [evidence_id, observed] : observed_doc_evidence) {
    if (!expected_doc_evidence.contains(evidence_id)) {
      check(false, "unexpected documentation evidence " + evidence_id +
                       ": observed " + std::to_string(observed));
    }
  }
}

template <typename TestBody>
int run(TestBody body) {
  failures = 0;
  configure_doc_evidence();
  try {
    body();
  } catch (const std::exception& error) {
    check(false, std::string("uncaught exception: ") + error.what());
  } catch (...) {
    check(false, "uncaught non-standard exception");
  }
  verify_doc_evidence();
  std::cout << (failures == 0 ? "all tests passed\n" : "TESTS FAILED\n");
  return failures == 0 ? 0 : 1;
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
int main() {
  return test::run([] { TESTS(); });
}
#endif
