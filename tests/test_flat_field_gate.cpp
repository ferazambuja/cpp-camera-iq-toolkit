#include <array>
#include <cmath>
#include <limits>
#include <vector>

#include "camera_iq/flat_field_gate.hpp"
#include "harness.hpp"

using camera_iq::flat_field_near_ceiling_passes;
using camera_iq::kFlatFieldMaxNearCeilingFraction;
using camera_iq::kFlatFieldMinFiniteCoverage;
using camera_iq::kFlatFieldNearCeilingLevel;
using camera_iq::measure_cfa_near_ceiling;
using camera_iq::RoiRect;
using test::check;
using test::check_near;

namespace {

constexpr int kWidth = 8;
constexpr int kHeight = 6;
const std::array<double, 4> kCeilings{100.0, 100.0, 100.0, 100.0};

// Mosaic position index for a pixel. This shared helper is deliberately
// position-only; color labels require the caller's CFA metadata.
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
    const auto measurement =
        measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth, gate,
                                 kCeilings, kFlatFieldNearCeilingLevel);
    check(measurement.has_value(), "gate: even Bayer geometry is measurable");
    for (std::size_t p = 0; p < 4; ++p) {
      check_near(measurement->fraction_frame[p], 0.0, 1e-12,
                 "gate: frame well below the level reads zero");
      check_near(measurement->fraction_gate[p], 0.0, 1e-12,
                 "gate: centered region well below the level reads zero");
    }
    check(flat_field_near_ceiling_passes(
              measurement->fraction_frame[0], measurement->fraction_gate[0],
              measurement->finite_fraction_frame[0],
              measurement->finite_fraction_gate[0],
              kFlatFieldMaxNearCeilingFraction, kFlatFieldMinFiniteCoverage),
          "gate: a clean plane passes the shared predicate");
  }

  {
    // An odd active dimension cannot be split into balanced 2x2 positions.
    // `shading` rejects such a mosaic outright rather than publish a trimmed
    // frame, and `patches` must agree: it corrects every pixel in the image,
    // so a frame trimmed for screening would leave the last row or column
    // corrected but never screened.
    const std::vector<double> odd_w(static_cast<std::size_t>(7) * kHeight,
                                    50.0);
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_cfa_balanced_roi,
        check(!measure_cfa_near_ceiling(odd_w.data(), 7, kHeight, 7, gate,
                                        kCeilings, kFlatFieldNearCeilingLevel)
                   .has_value(),
              "gate: odd active width is rejected, not trimmed out of screening"));

    const std::vector<double> odd_h(static_cast<std::size_t>(kWidth) * 5, 50.0);
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_cfa_balanced_roi,
        check(!measure_cfa_near_ceiling(odd_h.data(), kWidth, 5, kWidth, gate,
                                        kCeilings, kFlatFieldNearCeilingLevel)
                   .has_value(),
              "gate: odd active height is rejected, not trimmed out of screening"));

    // The caller's declared gate is a separate matter: an unbalanced or
    // out-of-frame gate is rejected rather than quietly re-aligned.
    const auto frame = flat_frame(50.0);
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_cfa_balanced_roi,
        check(!measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth,
                                        RoiRect{1, 2, 4, 2}, kCeilings,
                                        kFlatFieldNearCeilingLevel)
                   .has_value(),
              "gate: an odd-origin gate is rejected, not re-aligned"));
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_cfa_balanced_roi,
        check(!measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth,
                                        RoiRect{6, 2, 4, 2}, kCeilings,
                                        kFlatFieldNearCeilingLevel)
                   .has_value(),
              "gate: a gate escaping the frame is rejected"));
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
    const auto measurement =
        measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth, gate,
                                 kCeilings, kFlatFieldNearCeilingLevel);
    check(measurement.has_value(),
          "gate: one saturated position stays measurable");
    check(target == 2, "gate: (even x, odd y) is position 2");
    for (std::size_t p = 0; p < 4; ++p) {
      const double expected = p == target ? 1.0 : 0.0;
      check_near(measurement->fraction_frame[p], expected, 1e-12,
                 "gate: frame fraction isolates the saturated position");
      check_near(measurement->fraction_gate[p], expected, 1e-12,
                 "gate: gate fraction isolates the saturated position");
    }
    check(!flat_field_near_ceiling_passes(
              measurement->fraction_frame[target],
              measurement->fraction_gate[target],
              measurement->finite_fraction_frame[target],
              measurement->finite_fraction_gate[target],
              kFlatFieldMaxNearCeilingFraction, kFlatFieldMinFiniteCoverage),
          "gate: the saturated position alone fails the predicate");
    check(flat_field_near_ceiling_passes(
              measurement->fraction_frame[0], measurement->fraction_gate[0],
              measurement->finite_fraction_frame[0],
              measurement->finite_fraction_gate[0],
              kFlatFieldMaxNearCeilingFraction, kFlatFieldMinFiniteCoverage),
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
    const auto measurement =
        measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth, gate,
                                 kCeilings, kFlatFieldNearCeilingLevel);
    check(measurement.has_value(),
          "gate: an all-missing plane still returns the other three");
    check(std::isnan(measurement->fraction_frame[target]),
          "gate: a plane with no finite samples reports NaN, not zero");
    check(!flat_field_near_ceiling_passes(
              measurement->fraction_frame[target],
              measurement->fraction_gate[target],
              measurement->finite_fraction_frame[target],
              measurement->finite_fraction_gate[target],
              kFlatFieldMaxNearCeilingFraction, kFlatFieldMinFiniteCoverage),
          "gate: an unmeasurable plane cannot masquerade as a clean flat");
    for (std::size_t p = 0; p < 4; ++p) {
      if (p == target) continue;
      check_near(measurement->fraction_frame[p], 0.0, 1e-12,
                 "gate: one missing plane does not disturb the others");
    }
  }

  {
    // A near-ceiling fraction is a ratio over finite samples only, so a plane
    // reduced to a single finite low sample reads 0/1 = 0 and looks pristine.
    // The other samples are not skipped downstream: apply_flat_field() replaces
    // a non-finite denominator with the floor, producing an enormous correction
    // gain. Coverage must therefore gate acceptance alongside the fraction.
    auto frame = flat_frame(std::numeric_limits<double>::quiet_NaN());
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        frame[static_cast<std::size_t>(y) * kWidth + x] = 1.0;
      }
    }
    // Keep the gate itself fully finite so only whole-frame coverage is thin.
    for (int y = gate.y; y < gate.y + gate.height; ++y) {
      for (int x = gate.x; x < gate.x + gate.width; ++x) {
        frame[static_cast<std::size_t>(y) * kWidth + x] = 1.0;
      }
    }
    const auto measurement =
        measure_cfa_near_ceiling(frame.data(), kWidth, kHeight, kWidth, gate,
                                 kCeilings, kFlatFieldNearCeilingLevel);
    check(measurement.has_value(), "gate: a sparse frame is still measurable");
    for (std::size_t p = 0; p < 4; ++p) {
      check_near(measurement->fraction_frame[p], 0.0, 1e-12,
                 "gate: a sparse plane's near-ceiling fraction reads zero");
      check(measurement->finite_fraction_frame[p] < kFlatFieldMinFiniteCoverage,
            "gate: sparse whole-frame coverage is reported and below policy");
      check_near(measurement->finite_fraction_gate[p], 1.0, 1e-12,
                 "gate: the fully finite gate reports complete coverage");
      check(!flat_field_near_ceiling_passes(
                measurement->fraction_frame[p], measurement->fraction_gate[p],
                measurement->finite_fraction_frame[p],
                measurement->finite_fraction_gate[p],
                kFlatFieldMaxNearCeilingFraction, kFlatFieldMinFiniteCoverage),
            "gate: a zero fraction over too few samples is not a clean flat");
    }
  }

  {
    // Measure the declared inclusive threshold rather than injecting a ratio.
    // A 20x20 mosaic has exactly 100 samples per CFA position.
    constexpr int width = 20;
    constexpr int height = 20;
    const RoiRect full{0, 0, width, height};
    std::vector<double> frame(static_cast<std::size_t>(width) * height, 50.0);
    for (int y = 0; y < 2; ++y) {
      for (int x = 0; x < 2; ++x) {
        frame[static_cast<std::size_t>(y) * width + x] = 99.0;
      }
    }
    auto measurement =
        measure_cfa_near_ceiling(frame.data(), width, height, width, full,
                                 kCeilings, kFlatFieldNearCeilingLevel);
    check(measurement.has_value(), "gate: exact one-percent fixture measures");
    for (std::size_t p = 0; p < 4; ++p) {
      CAMERA_IQ_DOC_EVIDENCE(
          flat_field_threshold_boundaries,
          check_near(measurement->fraction_frame[p], 0.01, 1e-12,
                     "gate: one of 100 samples measures exactly one percent"));
      CAMERA_IQ_DOC_EVIDENCE(
          flat_field_threshold_boundaries,
          check(flat_field_near_ceiling_passes(
                    measurement->fraction_frame[p],
                    measurement->fraction_gate[p],
                    measurement->finite_fraction_frame[p],
                    measurement->finite_fraction_gate[p], 0.01, 0.9),
                "gate: measured one-percent plane is accepted inclusively"));
    }

    for (int y = 2; y < 4; ++y) {
      for (int x = 0; x < 2; ++x) {
        frame[static_cast<std::size_t>(y) * width + x] = 99.0;
      }
    }
    measurement =
        measure_cfa_near_ceiling(frame.data(), width, height, width, full,
                                 kCeilings, kFlatFieldNearCeilingLevel);
    check(measurement.has_value(), "gate: two-percent fixture measures");
    for (std::size_t p = 0; p < 4; ++p) {
      CAMERA_IQ_DOC_EVIDENCE(
          flat_field_threshold_boundaries,
          check_near(measurement->fraction_frame[p], 0.02, 1e-12,
                     "gate: two of 100 samples measures exactly two percent"));
      CAMERA_IQ_DOC_EVIDENCE(
          flat_field_threshold_boundaries,
          check(!flat_field_near_ceiling_passes(
                    measurement->fraction_frame[p],
                    measurement->fraction_gate[p],
                    measurement->finite_fraction_frame[p],
                    measurement->finite_fraction_gate[p], 0.01, 0.9),
                "gate: measured two-percent plane is rejected"));
    }
  }

  {
    // An invalid policy is not a licence to accept. Every argument runs through
    // the same finite/range validation as the measured fractions.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    check(!flat_field_near_ceiling_passes(0.0, 0.0, 1.0, 1.0, nan, 0.9),
          "gate: a non-finite policy rejects");
    check(!flat_field_near_ceiling_passes(0.0, 0.0, 1.0, 1.0, 1.5, 0.9),
          "gate: an out-of-range policy rejects");
    check(!flat_field_near_ceiling_passes(-0.1, 0.0, 1.0, 1.0, 0.01, 0.9),
          "gate: a negative fraction rejects");
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_threshold_boundaries,
        check(flat_field_near_ceiling_passes(0.01, 0.01, 1.0, 1.0, 0.01, 0.9),
              "gate: exactly at policy is not a rejection"));
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_threshold_boundaries,
        check(!flat_field_near_ceiling_passes(std::nextafter(0.01, 1.0), 0.01,
                                              1.0, 1.0, 0.01, 0.9),
              "gate: frame fraction immediately above policy is rejected"));
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_threshold_boundaries,
        check(!flat_field_near_ceiling_passes(0.01, std::nextafter(0.01, 1.0),
                                              1.0, 1.0, 0.01, 0.9),
              "gate: center fraction immediately above policy is rejected"));
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_threshold_boundaries,
        check(flat_field_near_ceiling_passes(0.0, 0.0, 0.9, 0.9, 0.01, 0.9),
              "gate: coverage exactly at policy is accepted"));
    CAMERA_IQ_DOC_EVIDENCE(
        flat_field_threshold_boundaries,
        check(!flat_field_near_ceiling_passes(
                  0.0, 0.0, std::nextafter(0.9, 0.0), 0.9, 0.01, 0.9),
              "gate: coverage immediately below policy is rejected"));
  }
}
