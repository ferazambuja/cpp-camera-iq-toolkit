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

  {
    const char* args[] = {"camera_iq", "dark-calibration",
                          "/definitely/not/a/camera_iq/dataset"};
    check(camera_iq::run(3, const_cast<char**>(args)) == 1,
          "dark-calibration command is routed");
  }
  {
    const char* args[] = {"camera_iq", "oecf-fit",
                          "/definitely/not/a/camera_iq/dataset"};
    check(camera_iq::run(3, const_cast<char**>(args)) == 1,
          "oecf-fit command is routed");
  }
  {
    const char* args[] = {"camera_iq", "ccm-fit", "missing_dataset",
                          "--illuminant-spd", "/definitely/not/a/spectrum.csv"};
    check(camera_iq::run(5, const_cast<char**>(args)) == 1,
          "ccm-fit command is routed");
  }
  {
    const char* args[] = {"camera_iq", "dark-calibration",
                          "/definitely/not/a/camera_iq/dataset",
                          "--residual-tolerance", "bad"};
    bool returned_usage = false;
    try {
      returned_usage = camera_iq::run(5, const_cast<char**>(args)) == 2;
    } catch (...) {
      returned_usage = false;
    }
    check(returned_usage, "dark-calibration rejects bad tolerance cleanly");
  }

  std::cout << (failures == 0 ? "all tests passed\n" : "TESTS FAILED\n");
  return failures == 0 ? 0 : 1;
}
