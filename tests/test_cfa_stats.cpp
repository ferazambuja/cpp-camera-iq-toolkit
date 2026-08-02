#include "camera_iq/cfa_stats.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
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

  // --- correctness: high DN, tiny spread ---
  // Not a cancellation guard: raw values are uint16, so sumsq stays well within
  // double's exact-integer range at any realistic sensor size and the naive
  // form would pass this too. It exists to check mean/stddev correctness in the
  // high-DN/small-spread corner. The Welford recurrence's cancellation
  // resistance (which matters for 16-bit-class accumulation) is genuinely
  // exercised by the 1e8-scale double test in test_demosaic.
  {
    std::vector<std::uint16_t> data = {
        12000, 13000,
        13000, 13000,
        12001, 13000,
        13000, 13000,
    };
    const std::array<double, 4> zero_black = {0, 0, 0, 0};
    const auto s = cfa_plane_stats(data.data(), 2, 4, kRGGB, kCdesc,
                                   zero_black, 16383);
    check(s[0].count == 2, "hi-DN: R sampled twice");
    check_near(s[0].mean, 12000.5, 1e-12, "hi-DN: mean");
    check_near(s[0].stddev, 0.5, 1e-12, "hi-DN: small stddev");
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

  // --- near-ceiling: the sensor plateau can sit below the metadata white ---
  //
  // The measured Fuji X-T100 frame pins at RAW 16381 while white_level reports
  // 16383, so an exact `raw >= white` test reports zero saturation despite no
  // recorded within-frame variation. near_ceiling_fraction measures the
  // signal-referred headroom instead: residual >= level * (white - black). This
  // fixture does not identify clipping or response compression by itself.
  //
  // These are not invented numbers. `Sphere_f8.0_1:10_DSCF0369.RAF` in the
  // private CLRS-589 archive holds all 24,148,224 active pixels at exactly this
  // plateau, giving stddev 0 with saturated_fraction 0 on every plane. The
  // archive is not distributed, so this fixture is what CI can run; the
  // measurement it stands in for is in docs/reports/RAW_STATS.md.
  {
    const std::array<double, 4> black = {1024, 1024, 1024, 1024};
    const double white = 16383;  // signal-referred ceiling 15359, 98% -> 15052

    std::vector<std::uint16_t> pinned = {16381, 16381, 16381, 16381};
    const auto s = cfa_plane_stats(pinned.data(), 2, 2, kRGGB, kCdesc, black,
                                   white);
    for (int p = 0; p < 4; ++p) {
      check(s[p].saturated_fraction == 0,
            "near-ceiling: exact saturation test still reports zero");
      check_near(s[p].near_ceiling_fraction, 1.0, 1e-9,
                 "near-ceiling: plateau below white is fully flagged");
    }

    // 16075 - 1024 = 15051, one DN under the 98% threshold.
    std::vector<std::uint16_t> under = {16075, 16075, 16075, 16075};
    const auto below = cfa_plane_stats(under.data(), 2, 2, kRGGB, kCdesc, black,
                                       white);
    check(below[0].near_ceiling_fraction == 0,
          "near-ceiling: one DN under the threshold is not flagged");

    std::vector<std::uint16_t> boundary = {16076, 16076, 16074, 16072};
    const std::array<double, 4> unequal_black = {1024, 1000, 900, 800};
    const auto per_plane = cfa_plane_stats(boundary.data(), 2, 2, kRGGB,
                                           kCdesc, unequal_black, white);
    for (int p = 0; p < 4; ++p) {
      check_near(per_plane[p].near_ceiling_fraction, 1.0, 1e-9,
                 "near-ceiling: threshold uses each plane's white-minus-black");
      check(per_plane[p].saturated_fraction == 0,
            "near-ceiling: boundary sample remains below exact white");
    }

    std::vector<std::uint16_t> exact_white = {16383, 16383, 16383, 16383};
    const auto exact = cfa_plane_stats(exact_white.data(), 2, 2, kRGGB, kCdesc,
                                       black, white);
    for (int p = 0; p < 4; ++p) {
      check_near(exact[p].saturated_fraction, 1.0, 1e-9,
                 "near-ceiling: exact white remains saturated");
      check_near(exact[p].near_ceiling_fraction, 1.0, 1e-9,
                 "near-ceiling: exact white is also near ceiling");
    }

    // The threshold is a caller policy, not a constant baked into the stats.
    const auto loose = cfa_plane_stats(under.data(), 2, 2, kRGGB, kCdesc, black,
                                       white, 0.90);
    check_near(loose[0].near_ceiling_fraction, 1.0, 1e-9,
               "near-ceiling: level is configurable");

    const std::array<double, 5> invalid_levels = {
        0.0, -0.1, 1.01, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()};
    for (double level : invalid_levels) {
      const auto invalid = cfa_plane_stats(under.data(), 2, 2, kRGGB, kCdesc,
                                           black, white, level);
      for (const auto& plane : invalid) {
        check(std::isnan(plane.near_ceiling_fraction),
              "near-ceiling: invalid policy produces an explicit undefined value");
      }
    }

    const auto invalid_empty = cfa_plane_stats(
        nullptr, 0, 0, kRGGB, kCdesc, black, white, 0.0);
    for (const auto& plane : invalid_empty) {
      check(std::isnan(plane.near_ceiling_fraction),
            "near-ceiling: invalid policy stays explicit on empty input");
    }
  }

  // --- the threshold rule itself, shared by the full-frame and ROI paths ---
  //
  // RAW_STATS.md states both paths apply the same per-plane
  // `white_level - black[p]` definition. They now call this one function, so
  // the claim holds by construction instead of by two implementations agreeing
  // today. These cases pin what that function means.
  {
    using camera_iq::near_ceiling_threshold;
    const auto ok = near_ceiling_threshold(16383, 1024, 0.98);
    check(ok.has_value(), "threshold: defined for ordinary metadata");
    if (ok) {
      check_near(*ok, 0.98 * 15359, 1e-9,
                 "threshold: level times signal-referred ceiling");
    }
    check(!near_ceiling_threshold(1024, 1024, 0.98).has_value(),
          "threshold: undefined when white equals black");
    check(!near_ceiling_threshold(1000, 1024, 0.98).has_value(),
          "threshold: undefined when white is below black");
    check(!near_ceiling_threshold(16383, 1024, 0.0).has_value(),
          "threshold: undefined at level zero");
    check(!near_ceiling_threshold(16383, 1024, 1.5).has_value(),
          "threshold: undefined above level one");
    check(!near_ceiling_threshold(std::numeric_limits<double>::quiet_NaN(),
                                  1024, 0.98)
               .has_value(),
          "threshold: undefined for non-finite white");
    check(near_ceiling_threshold(16383, 1024).has_value(),
          "threshold: default level is the raw-stats policy");
  }
}
