#include "camera_iq/cfa_stats.hpp"

#include <cstdint>
#include <vector>

#include "harness.hpp"

using camera_iq::cfa_plane_stats;
using camera_iq::cfa_plane_stats_strided;
using camera_iq::channel_labels;
using test::check;
using test::check_near;

namespace {
// RGGB: cdesc "RGBG", COLOR indices at (0,0)(0,1)(1,0)(1,1) = {0,1,3,2}
// -> letters R,G,G,B.
const std::string kCdesc = "RGBG";
const std::array<int, 4> kRGGB = {0, 1, 3, 2};
const std::array<double, 4> kBlack = {1024, 1024, 1024, 1024};
}  // namespace

void TESTS() {
  // --- channel labels ---
  const auto labels = channel_labels(kCdesc, kRGGB);
  check(labels[0] == "R" && labels[1] == "G1" && labels[2] == "G2" &&
            labels[3] == "B",
        "labels: RGGB -> R,G1,G2,B (greens disambiguated)");
  const auto grbg = channel_labels(kCdesc, {1, 0, 2, 3});  // G,R,B,G
  check(grbg[0] == "G1" && grbg[1] == "R" && grbg[2] == "B" && grbg[3] == "G2",
        "labels: GRBG greens ordered by position");

  // --- basic 2x2, one pixel per position, black-subtracted ---
  {
    // positions (0,0)=R (0,1)=G1 (1,0)=G2 (1,1)=B
    std::vector<std::uint16_t> data = {2000, 1500, 1600, 1200};
    const auto s = cfa_plane_stats(data.data(), 2, 2, kRGGB, kCdesc, kBlack,
                                   16383);
    check(s[0].label == "R" && s[0].count == 1, "2x2: R present, count 1");
    check_near(s[0].mean, 976, 1e-9, "2x2: R = 2000-1024");
    check_near(s[1].mean, 476, 1e-9, "2x2: G1 = 1500-1024");
    check_near(s[2].mean, 576, 1e-9, "2x2: G2 = 1600-1024");
    check_near(s[3].mean, 176, 1e-9, "2x2: B = 1200-1024");
    check(s[0].stddev == 0 && s[0].min == s[0].max, "2x2: single pixel std 0");
    check(s[0].saturated_fraction == 0, "2x2: nothing saturated");
  }

  // --- signed residuals: raw below black is preserved, not clamped ---
  {
    std::vector<std::uint16_t> data = {500, 1024, 1024, 1024};  // R=500<black
    const auto s = cfa_plane_stats(data.data(), 2, 2, kRGGB, kCdesc, kBlack,
                                   16383);
    check_near(s[0].mean, -524, 1e-9, "signed: 500-1024 -> -524");
    check_near(s[0].min, -524, 1e-9, "signed: min keeps below-black residual");
    check_near(s[0].below_black_fraction, 1.0, 1e-9,
               "signed: below-black fraction recorded");
  }

  // --- saturation: RAW >= white counts, tested pre-subtraction ---
  {
    std::vector<std::uint16_t> data = {16383, 1024, 1024, 1024};
    const auto s = cfa_plane_stats(data.data(), 2, 2, kRGGB, kCdesc, kBlack,
                                   16383);
    check_near(s[0].saturated_fraction, 1.0, 1e-9,
               "sat: R at white -> fraction 1.0");
    check(s[1].saturated_fraction == 0, "sat: others not saturated");
  }

  // --- mean/std over two pixels per position (4x2) ---
  {
    // R (pos0) at (0,0)=1124 (2,0)=1324 -> subtracted 100,300 -> mean 200 std 100
    std::vector<std::uint16_t> data = {
        1124, 1024,  // row0: (0,0)R  (0,1)G1
        1024, 1024,  // row1: (1,0)G2 (1,1)B
        1324, 1024,  // row2: (0,0)R  (0,1)G1
        1024, 1024,  // row3: (1,0)G2 (1,1)B
    };
    const auto s = cfa_plane_stats(data.data(), 2, 4, kRGGB, kCdesc, kBlack,
                                   16383);
    check(s[0].count == 2, "4x2: R sampled twice");
    check_near(s[0].mean, 200, 1e-9, "4x2: R mean 200");
    check_near(s[0].stddev, 100, 1e-9, "4x2: R stddev 100");
    check_near(s[0].min, 100, 1e-9, "4x2: R min 100");
    check_near(s[0].max, 300, 1e-9, "4x2: R max 300");
  }

  // --- empty / null guard ---
  {
    const auto s = cfa_plane_stats(nullptr, 0, 0, kRGGB, kCdesc, kBlack, 16383);
    check(s[0].count == 0 && s[0].mean == 0, "empty: no crash, zero stats");
  }

  // --- active-area crop via stride: masked border pixels are excluded ---
  {
    // 4x4 raw buffer with a 2x2 active area at row=1,col=1. Border pixels are
    // deliberately extreme; stats must see only the active area through stride.
    std::vector<std::uint16_t> data = {
        9999, 9999, 9999, 9999,
        9999, 10,   20,   9999,
        9999, 30,   40,   9999,
        9999, 9999, 9999, 9999,
    };
    const std::array<double, 4> zero_black = {0, 0, 0, 0};
    const auto s = cfa_plane_stats_strided(data.data() + 5, 2, 2, 4, kRGGB,
                                           kCdesc, zero_black, 1000);
    check(s[0].count == 1 && s[1].count == 1 && s[2].count == 1 &&
              s[3].count == 1,
          "stride: active 2x2 only, one sample per CFA position");
    check_near(s[0].mean, 10, 1e-9, "stride: R from active origin");
    check_near(s[1].mean, 20, 1e-9, "stride: G1 from active origin");
    check_near(s[2].mean, 30, 1e-9, "stride: G2 from active origin");
    check_near(s[3].mean, 40, 1e-9, "stride: B from active origin");
    check(s[0].saturated_fraction == 0, "stride: masked border not saturated");
  }
}
