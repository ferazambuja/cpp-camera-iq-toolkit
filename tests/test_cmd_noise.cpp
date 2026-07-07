#include "camera_iq/commands.hpp"

#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::cmd_noise;
using test::check;

namespace {

int run_noise(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return cmd_noise(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

void TESTS() {
  check(run_noise({}) == 2, "noise command: dataset argument required");
  check(run_noise({"missing_dataset", "--subdir"}) == 2,
        "noise command: subdir requires value");
  check(run_noise({"missing_dataset", "--roi", "bad"}) == 2,
        "noise command: malformed ROI rejected before dataset I/O");
  check(run_noise({"missing_dataset", "--residual-tolerance", "bad"}) == 2,
        "noise command: bad residual tolerance rejected");
  check(run_noise({"missing_dataset", "--subdir", "Images/Dark Frame"}) == 1,
        "noise command: valid syntax reaches dataset resolution");
}
