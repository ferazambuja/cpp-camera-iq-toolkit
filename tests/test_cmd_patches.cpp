#include "camera_iq/commands.hpp"

#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::cmd_patches;
using test::check;

namespace {

int run_patches(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return cmd_patches(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

void TESTS() {
  const std::string valid_corners = "0,0;242,0;242,172;0,172";

  check(run_patches({"missing.RAF", "--sg-corners", valid_corners}) == 1,
        "patches command: valid sg-corners accepted as coordinate source");
  check(run_patches({"missing.RAF", "--sg-corners", valid_corners,
                     "--coords", "coord.csv"}) == 2,
        "patches command: sg-corners mutually exclusive with coords");
  check(run_patches({"missing.RAF", "--sg-corners", valid_corners,
                     "--rawdigger-csv", "rawdigger.csv"}) == 2,
        "patches command: sg-corners mutually exclusive with RawDigger CSV");
  check(run_patches({"missing.RAF", "--coords", "coord.csv",
                     "--rawdigger-oracle-csv", "rawdigger.csv"}) == 2,
        "patches command: RawDigger oracle requires sg-corners");
  check(run_patches({"missing.RAF", "--sg-corners", valid_corners,
                     "--rawdigger-oracle-csv", "rawdigger.csv",
                     "--flat-field-raw", "flat.RAF"}) == 2,
        "patches command: RawDigger oracle rejects corrected extraction");
  check(run_patches({"missing.RAF", "--sg-corner-source", "manual"}) == 2,
        "patches command: sg-corner-source requires sg-corners");
  check(run_patches({"missing.RAF", "--sg-corners", "0,0;bad;242,172;0,172"}) ==
            2,
        "patches command: malformed sg-corners rejected before RAW I/O");
  check(run_patches({"missing.RAF", "--sg-corners"}) == 2,
        "patches command: sg-corners requires a value");
}
