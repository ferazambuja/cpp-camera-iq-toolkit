#pragma once

#include <string>

namespace camera_iq {

// Toolkit version string (defined from the CMake project version).
std::string version();

// CLI entry point. Parses argv and dispatches subcommands.
// Returns a process exit code.
int run(int argc, char** argv);

}  // namespace camera_iq
