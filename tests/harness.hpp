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

template <typename TestBody>
int run(TestBody body) {
  failures = 0;
  try {
    body();
  } catch (const std::exception& error) {
    check(false, std::string("uncaught exception: ") + error.what());
  } catch (...) {
    check(false, "uncaught non-standard exception");
  }
  std::cout << (failures == 0 ? "all tests passed\n" : "TESTS FAILED\n");
  return failures == 0 ? 0 : 1;
}

}  // namespace test

void TESTS();

#ifndef CAMERA_IQ_TEST_HARNESS_NO_MAIN
int main() {
  return test::run([] { TESTS(); });
}
#endif
