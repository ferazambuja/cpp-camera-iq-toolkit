#include "camera_iq/toolkit.hpp"

#include <iostream>
#include <string>

// Minimal dependency-free test harness. Later phases can adopt a framework.
static int failures = 0;

static void check(bool condition, const std::string& name) {
  if (condition) {
    std::cout << "[ ok ] " << name << "\n";
  } else {
    std::cout << "[fail] " << name << "\n";
    ++failures;
  }
}

int main() {
  check(!camera_iq::version().empty(), "version is non-empty");
  check(camera_iq::version() != "0.0.0", "version comes from CMake project");

  std::cout << (failures == 0 ? "all tests passed\n" : "TESTS FAILED\n");
  return failures == 0 ? 0 : 1;
}
