#pragma once

// Minimal dependency-free test harness shared by all test executables.
// Each test .cpp defines TESTS() and includes this header, which supplies main().

#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>

namespace test {

inline int failures = 0;

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

}  // namespace test

void TESTS();

int main() {
  // An escaping exception would otherwise reach std::terminate, which aborts
  // without the summary line and without the accumulated failures, so a
  // regression that throws would be reported only as a signal. Catching it
  // keeps the diagnosis in the log and the exit status meaningful.
  try {
    TESTS();
  } catch (const std::exception& error) {
    test::check(false, std::string("uncaught exception: ") + error.what());
  } catch (...) {
    test::check(false, "uncaught non-standard exception");
  }
  std::cout << (test::failures == 0 ? "all tests passed\n" : "TESTS FAILED\n");
  return test::failures == 0 ? 0 : 1;
}
