#include "camera_iq/toolkit.hpp"

#include <iostream>
#include <sstream>
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
    std::ostringstream captured;
    auto* old_buf = std::cout.rdbuf(captured.rdbuf());
    const char* args[] = {"camera_iq", "--help"};
    const int rc = camera_iq::run(2, const_cast<char**>(args));
    std::cout.rdbuf(old_buf);
    const std::string help = captured.str();
    check(rc == 0, "help command succeeds");
    check(help.find("Commands (planned)") == std::string::npos &&
              help.find("  noise") != std::string::npos,
          "help lists implemented noise command and no planned block");
  }

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
    const char* args[] = {"camera_iq", "patches",
                          "/definitely/not/a/camera_iq/raw.RAF",
                          "--coords", "/definitely/not/a/coord.csv"};
    check(camera_iq::run(5, const_cast<char**>(args)) == 1,
          "patches command is routed");
  }
  {
    const char* args[] = {"camera_iq", "noise",
                          "/definitely/not/a/camera_iq/dataset"};
    check(camera_iq::run(3, const_cast<char**>(args)) == 1,
          "noise command is routed");
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
