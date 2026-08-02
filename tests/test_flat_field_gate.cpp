#include "camera_iq/flat_field_gate.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "harness.hpp"

using camera_iq::RoiRect;
using camera_iq::flat_field_near_ceiling_passes;
using camera_iq::kFlatFieldMaxNearCeilingFraction;
using camera_iq::kFlatFieldNearCeilingLevel;
using camera_iq::measure_cfa_near_ceiling;
using test::check;
using test::check_near;

namespace {

constexpr int kWidth = 8;
constexpr int kHeight = 6;
const std::array<double, 4> kCeilings{100.0, 100.0, 100.0, 100.0};

// Mosaic position index for a pixel, in the R/G1/G2/B order the reports and the
// published summary CSV use.
std::size_t position_of(int x, int y) {
  return static_cast<std::size_t>((y % 2) * 2 + (x % 2));
}

std::vector<double> flat_frame(double value) {
  return std::vector<double>(static_cast<std::size_t>(kWidth) * kHeight, value);
}

}  // namespace

void TESTS() {
  // Even origin and size, so every CFA position contributes the same sample
  // count. That balance is what makes the four fractions comparable.
  const RoiRect gate{2, 2, 4, 2};

  {
    const auto frame = flat_frame(50.0);
    const auto measurement = measure_cfa_near_ceiling(
        frame.data(), kWidth, kHeight, kWidth, gate, kCeilings,
        kFlatFieldNearCeilingLevel);
    check(measurement.has_value(), "gate: even Bayer geometry is measurable");
    for (std::size_t p = 0; p < 4; ++p) {
      check_near(measurement->fraction_frame[p], 0.0, 1e-12,
                 "gate: frame well below the level reads zero");
      check_near(measurement->fraction_gate[p], 0.0, 1e-12,
                 "gate: centered region well below the level reads zero");
    }
    check(flat_field_near_ceiling_passes(measurement->fraction_frame[0],
                                         measurement->fraction_gate[0],
                                         kFlatFieldMaxNearCeilingFraction),
          "gate: a clean plane passes the shared predicate");
  }

  {
    // An odd active dimension cannot be split into balanced 2x2 positions, so
    // the measurement trims to an even rectangle rather than letting one
    // position's denominator run a row or column longer than the others. The
    // trim is not silent: the effective frame is returned and serialized, the
    // same way `shading` publishes its own geometry.
    const std::vector<double> odd_w(static_cast<std::size_t>(7) * kHeight, 50.0);
    const auto trimmed_w =
        measure_cfa_near_ceiling(odd_w.data(), 7, kHeight, 7, gate, kCeilings,
                                 kFlatFieldNearCeilingLevel);
    check(trimmed_w.has_value(), "gate: odd active width stays measurable");
    check(trimmed_w->frame.width == 6 && trimmed_w->frame.height == kHeight,
          "gate: odd active width trims to an even effective frame");

    const std::vector<double> odd_h(static_cast<std::size_t>(kWidth) * 5, 50.0);
    const auto trimmed_h = measure_cfa_near_ceiling(
        odd_h.data(), kWidth, 5, kWidth, gate, kCeilings,
        kFlatFieldNearCeilingLevel);
    check(trimmed_h.has_value(), "gate: odd active height stays measurable");
    check(trimmed_h->frame.width == kWidth && trimmed_h->frame.height == 4,
          "gate: odd active height trims to an even effective frame");

    // The caller's declared gate is a different matter. An unbalanced or
    // out-of-frame gate is rejected rather than quietly re-aligned, so a caller
    // is never handed measurements for geometry it did not ask for.
    const auto frame = flat_frame(50.0);
    check(!measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth,
                                    RoiRect{1, 2, 4, 2}, kCeilings,
                                    kFlatFieldNearCeilingLevel)
               .has_value(),
          "gate: an odd-origin gate is rejected, not re-aligned");
    check(!measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth,
                                    RoiRect{6, 2, 4, 2}, kCeilings,
                                    kFlatFieldNearCeilingLevel)
               .has_value(),
          "gate: a gate escaping the frame is rejected");
  }

  {
    // Per-position isolation is the point of the shared gate. Pooling the four
    // planes into one scalar is what let a single clipped green position
    // through on Sphere_f8.0_1:500. Saturate exactly one position and the other
    // three must stay at zero.
    auto frame = flat_frame(50.0);
    const std::size_t target = position_of(0, 1);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        if (position_of(x, y) == target) {
          frame[static_cast<std::size_t>(y) * kWidth + x] = 99.0;
        }
      }
    }
    const auto measurement = measure_cfa_near_ceiling(
        frame.data(), kWidth, kHeight, kWidth, gate, kCeilings,
        kFlatFieldNearCeilingLevel);
    check(measurement.has_value(),
          "gate: one saturated position stays measurable");
    check(target == 2, "gate: (even x, odd y) is position 2, the G2 index");
    for (std::size_t p = 0; p < 4; ++p) {
      const double expected = p == target ? 1.0 : 0.0;
      check_near(measurement->fraction_frame[p], expected, 1e-12,
                 "gate: frame fraction isolates the saturated position");
      check_near(measurement->fraction_gate[p], expected, 1e-12,
                 "gate: gate fraction isolates the saturated position");
    }
    check(!flat_field_near_ceiling_passes(measurement->fraction_frame[target],
                                          measurement->fraction_gate[target],
                                          kFlatFieldMaxNearCeilingFraction),
          "gate: the saturated position alone fails the predicate");
    check(flat_field_near_ceiling_passes(measurement->fraction_frame[0],
                                         measurement->fraction_gate[0],
                                         kFlatFieldMaxNearCeilingFraction),
          "gate: an unaffected position still passes on its own");
  }

  {
    // The header states that a plane with no finite samples reports NaN and so
    // fails the predicate. An unmeasurable plane must never read as a clean
    // one, so pin that rather than trust the comment.
    auto frame = flat_frame(50.0);
    const std::size_t target = position_of(1, 1);
    for (int y = 0; y < kHeight; ++y) {
      for (int x = 0; x < kWidth; ++x) {
        if (position_of(x, y) == target) {
          frame[static_cast<std::size_t>(y) * kWidth + x] =
              std::numeric_limits<double>::quiet_NaN();
        }
      }
    }
    const auto measurement = measure_cfa_near_ceiling(
        frame.data(), kWidth, kHeight, kWidth, gate, kCeilings,
        kFlatFieldNearCeilingLevel);
    check(measurement.has_value(),
          "gate: an all-missing plane still returns the other three");
    check(std::isnan(measurement->fraction_frame[target]),
          "gate: a plane with no finite samples reports NaN, not zero");
    check(!flat_field_near_ceiling_passes(measurement->fraction_frame[target],
                                          measurement->fraction_gate[target],
                                          kFlatFieldMaxNearCeilingFraction),
          "gate: an unmeasurable plane cannot masquerade as a clean flat");
    for (std::size_t p = 0; p < 4; ++p) {
      if (p == target) continue;
      check_near(measurement->fraction_frame[p], 0.0, 1e-12,
                 "gate: one missing plane does not disturb the others");
    }
  }

  {
    // An invalid policy is not a licence to accept. Every argument runs through
    // the same finite/range validation as the measured fractions.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    check(!flat_field_near_ceiling_passes(0.0, 0.0, nan),
          "gate: a non-finite policy rejects");
    check(!flat_field_near_ceiling_passes(0.0, 0.0, 1.5),
          "gate: an out-of-range policy rejects");
    check(!flat_field_near_ceiling_passes(-0.1, 0.0, 0.01),
          "gate: a negative fraction rejects");
    check(flat_field_near_ceiling_passes(0.01, 0.01, 0.01),
          "gate: exactly at policy is not a rejection");
  }
}
