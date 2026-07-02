#pragma once

// Minimal dependency-free test harness shared by all test executables.
// Each test .cpp defines TESTS() and includes this header, which supplies main().

#include <cmath>
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
    std::cout << "       expected " << expected << ", got " << actual << "\n";
  }
  check(ok, name);
}

}  // namespace test

void TESTS();

int main() {
  TESTS();
  std::cout << (test::failures == 0 ? "all tests passed\n" : "TESTS FAILED\n");
  return test::failures == 0 ? 0 : 1;
}
