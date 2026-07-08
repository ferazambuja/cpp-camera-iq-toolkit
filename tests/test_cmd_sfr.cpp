#include "camera_iq/commands.hpp"

#include <string>
#include <vector>

#include "harness.hpp"

using camera_iq::cmd_sfr;
using test::check;

namespace {

int run_sfr(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return cmd_sfr(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

void TESTS() {
  check(run_sfr({}) == 2, "sfr command: dataset argument required");
  check(run_sfr({"missing_dataset", "--raw"}) == 2,
        "sfr command: raw requires a value");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF"}) == 2,
        "sfr command: ROI or oracle required");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "bad"}) == 2,
        "sfr command: malformed ROI rejected before dataset I/O");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "10,10,40,40"}) == 1,
        "sfr command: valid explicit ROI syntax reaches dataset resolution");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--oracle-y-multi",
                 "Results/edge__Y_multi.csv"}) == 1,
        "sfr command: valid oracle ROI syntax reaches dataset resolution");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--field-map"}) == 2,
        "sfr command: field-map requires oracle Y_multi");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--field-map",
                 "--oracle-y-multi", "Results/edge__Y_multi.csv"}) == 1,
        "sfr command: valid field-map syntax reaches dataset resolution");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--field-map",
                 "--oracle-y-multi", "Results/edge__Y_multi.csv",
                 "--edge-roi", "10,10,40,40"}) == 2,
        "sfr command: field-map rejects explicit edge ROI");
  check(run_sfr({"missing_dataset", "--raw", "edge.NEF", "--edge-roi",
                 "10,10,40,40", "--near-saturation-fraction", "1.5"}) == 2,
        "sfr command: bad saturation fraction rejected");
}
