#include "camera_iq/shading.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "camera_iq/raw_meta.hpp"
#include "harness.hpp"

using camera_iq::RawCfaImage;
using camera_iq::ShadingOptions;
using camera_iq::analyze_shading;
using camera_iq::chromatic_response;
using camera_iq::measure_shading_field;
using camera_iq::verify_shading_pedestal;
using test::check;
using test::check_near;

namespace {

// RGGB in LibRaw terms: COLOR() indices for (0,0)(0,1)(1,0)(1,1) into "RGBG".
const std::array<int, 4> kRGGB = {0, 1, 3, 2};
const std::string kCdesc = "RGBG";

using Bins = std::array<std::array<double, 4>, 4>;

// Bin-constant field, so every binned median is exact rather than approximate.
// A 128x128 mosaic gives 64x64 planes; a 4x4 grid gives 16x16 samples per bin.
// With center_frac 0.25 the centre region spans plane rows/cols 24..39, which
// is exactly the four central bins — so a matrix carrying 1.0 there normalizes
// to itself.
constexpr int kMosaic = 128;
constexpr int kGrid = 4;

std::vector<double> make_binned_mosaic(const std::array<double, 4>& amplitude,
                                       const std::array<const Bins*, 4>& field) {
  std::vector<double> m(static_cast<std::size_t>(kMosaic) * kMosaic, 0.0);
  for (int y = 0; y < kMosaic; ++y) {
    for (int x = 0; x < kMosaic; ++x) {
      const int p = (y % 2) * 2 + (x % 2);
      const int br = (y / 2) / (kMosaic / 2 / kGrid);
      const int bc = (x / 2) / (kMosaic / 2 / kGrid);
      m[static_cast<std::size_t>(y) * kMosaic + x] =
          amplitude[p] * (*field[p])[br][bc];
    }
  }
  return m;
}

bool maps_match(const std::vector<double>& actual, const Bins& expected,
                double tol) {
  if (actual.size() != static_cast<std::size_t>(kGrid) * kGrid) return false;
  for (int br = 0; br < kGrid; ++br) {
    for (int bc = 0; bc < kGrid; ++bc) {
      const double a = actual[static_cast<std::size_t>(br) * kGrid + bc];
      if (std::abs(a - expected[br][bc]) > tol) return false;
    }
  }
  return true;
}

}  // namespace

namespace {

// Builds a mosaic where CFA position p carries `plane_value[p]` scaled by a
// caller-supplied field. RGGB order: (0,0)=R (0,1)=G1 (1,0)=G2 (1,1)=B.
std::vector<double> make_mosaic(int width, int height,
                                const std::array<double, 4>& plane_value) {
  std::vector<double> m(static_cast<std::size_t>(width) * height, 0.0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const int p = (y % 2) * 2 + (x % 2);
      m[static_cast<std::size_t>(y) * width + x] = plane_value[p];
    }
  }
  return m;
}

}  // namespace

namespace {

// Signal-referred ceiling per CFA position: white_level - black[p]. Tests use a
// round number so near-ceiling arithmetic is checkable by hand.
const std::array<double, 4> kCeiling = {1000.0, 1000.0, 1000.0, 1000.0};

// Headroom ceiling for fixtures whose plane amplitudes exceed kCeiling: the
// near-ceiling gate is real, so a fixture that pins samples at the ceiling is
// rejected before any map is produced.
const std::array<double, 4> kHeadroom = {4000.0, 4000.0, 4000.0, 4000.0};

}  // namespace

void TESTS() {
  // Production entry point: callers provide a RawCfaImage, never a separately
  // supplied ceiling. This value sits above 98% of (white-black) but below 98%
  // of raw white, so only the correct signal-referred ceiling rejects it.
  {
    RawCfaImage image;
    image.width = 64;
    image.height = 64;
    image.row_stride_pixels = 64;
    image.samples.assign(64 * 64, 15100.0);
    image.meta.white_level = 16383.0;
    image.meta.black_per_channel = {1024.0, 1024.0, 1024.0, 1024.0};
    image.color_at_position = kRGGB;
    image.cdesc = kCdesc;

    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    const auto analysis = analyze_shading(image, opts);

    check_near(analysis.signal_ceiling[0], 15359.0, 1e-12,
               "high-level API: signal ceiling is white minus black");
    check(!analysis.field.valid,
          "high-level API: signal-referred near-ceiling gate rejects");
    check(!analysis.field.gates.near_ceiling_ok,
          "high-level API: CLI-safe path cannot use raw white by mistake");
  }

  // Invalid metadata/buffer combinations must be rejected before pointer math.
  {
    RawCfaImage image;
    image.width = 64;
    image.height = 64;
    image.row_stride_pixels = 64;
    image.samples.assign(12, 500.0);
    image.meta.white_level = 1000.0;
    image.color_at_position = kRGGB;
    image.cdesc = kCdesc;
    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    const auto analysis = analyze_shading(image, opts);
    check(!analysis.field.valid,
          "high-level API: undersized raw buffer is rejected");
    check(!analysis.field.rejection_reason.empty(),
          "high-level API: invalid image carries a diagnostic");
  }

  // The three geometries are explicit and CFA-balanced. Requested blocks that
  // cannot fit are rejected, never silently clamped into a different test.
  {
    const int width = 66;
    const int height = 62;
    const auto mosaic = make_mosaic(width, height, {500, 500, 500, 500});
    ShadingOptions opts;
    opts.grid_cols = 3;
    opts.grid_rows = 3;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 4;
    opts.gate_center_frac = 0.5;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             width, opts, kCeiling);
    check(field.geometry.valid, "geometry: effective regions are reported");
    check(field.geometry.center.x % 2 == 0 &&
              field.geometry.center.y % 2 == 0 &&
              field.geometry.center.width % 2 == 0 &&
              field.geometry.center.height % 2 == 0,
          "geometry: center block is CFA-balanced");
    check(field.geometry.corners[0].x == 4 &&
              field.geometry.corners[0].y == 4 &&
              field.geometry.corners[3].x == 46 &&
              field.geometry.corners[3].y == 42,
          "geometry: corner inset and block size are honored exactly");

    opts.corner_block_px = 60;
    opts.corner_inset_px = 4;
    const auto impossible = measure_shading_field(
        mosaic.data(), width, height, width, opts, kCeiling);
    check(!impossible.valid && !impossible.geometry.valid,
          "geometry: blocks that cannot fit are rejected");
    check(!impossible.rejection_reason.empty(),
          "geometry: fit rejection is diagnostic");
  }

  // Every public floating option is finite and range checked inside the core,
  // not only in the CLI parser. Grid size is bounded before allocation.
  {
    const int width = 64;
    const int height = 64;
    const auto mosaic = make_mosaic(width, height, {500, 500, 500, 500});
    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    opts.near_ceiling_max = std::numeric_limits<double>::quiet_NaN();
    const auto nan = measure_shading_field(mosaic.data(), width, height, width,
                                           opts, kCeiling);
    check(!nan.valid && !nan.rejection_reason.empty(),
          "options: NaN threshold is rejected by the core");

    opts.near_ceiling_max = 0.01;
    opts.grid_cols = 100000;
    const auto huge = measure_shading_field(mosaic.data(), width, height, width,
                                            opts, kCeiling);
    check(!huge.valid && !huge.rejection_reason.empty(),
          "options: grid finer than a CFA plane is rejected before allocation");
  }

  // Non-finite samples use the same geometric denominator as finite samples in
  // both near-ceiling fractions. Finiteness remains its own failing gate.
  {
    const int width = 64;
    const int height = 64;
    auto mosaic = make_mosaic(width, height, {500, 500, 500, 500});
    mosaic[static_cast<std::size_t>(16) * width + 16] = 1000.0;
    mosaic[static_cast<std::size_t>(18) * width + 18] =
        std::numeric_limits<double>::quiet_NaN();
    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    opts.gate_center_frac = 0.5;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             width, opts, kCeiling);
    check(!field.valid && !field.gates.finite_ok,
          "non-finite input: finiteness gate rejects");
    check_near(field.gates.near_ceiling_frac_gate[0], 1.0 / 256.0, 1e-12,
               "non-finite input: gate fraction keeps geometric denominator");
    check_near(field.gates.near_ceiling_frac_frame[0], 1.0 / 1024.0, 1e-12,
               "non-finite input: frame fraction keeps geometric denominator");
  }

  // A cropped view may have a row stride larger than its visible width.
  {
    const int width = 64;
    const int height = 64;
    const int stride = 70;
    std::vector<double> mosaic(static_cast<std::size_t>(stride) * height,
                               -9999.0);
    const std::array<double, 4> planes = {400, 500, 600, 700};
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        mosaic[static_cast<std::size_t>(y) * stride + x] =
            planes[(y % 2) * 2 + (x % 2)];
      }
    }
    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             stride, opts, kCeiling);
    check(field.valid, "stride: padded rows are accepted");
    check_near(field.center_block_median[3], 700.0, 1e-12,
               "stride: row padding is never sampled");
  }

  // The gate region and the center block are gated separately, and this is the
  // case that proves the gate region is the one that matters. Clipped samples
  // sit inside the gate region but outside the center block: 3.1% of the gate
  // region, 0.78% of the whole frame. A whole-frame-only guard accepts this
  // frame; normalizing by a center that a clip is creeping toward biases every
  // reported falloff toward less falloff. This is the archive's `1:500` frame
  // in miniature — 11.60% gate-region against 0.50% whole-frame.
  {
    const int width = 128;
    const int height = 128;
    std::vector<double> mosaic(static_cast<std::size_t>(width) * height, 500.0);
    for (int y = 36; y < 44; ++y) {
      for (int x = 36; x < 52; ++x) {
        mosaic[static_cast<std::size_t>(y) * width + x] = 1000.0;
      }
    }

    ShadingOptions opts;
    opts.grid_cols = 4;
    opts.grid_rows = 4;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 0;
    opts.gate_center_frac = 0.50;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             width, opts, kCeiling);

    check(!field.valid, "near-ceiling: gate-region clipping rejects the frame");
    check(!field.gates.near_ceiling_ok, "near-ceiling: gate verdict recorded");
    check_near(field.gates.near_ceiling_frac_gate[0], 32.0 / 1024.0, 1e-9,
               "near-ceiling: gate-region fraction reported");
    check_near(field.gates.near_ceiling_frac_frame[0], 32.0 / 4096.0, 1e-9,
               "near-ceiling: whole-frame fraction reported and lower");
    check(field.gates.near_ceiling_frac_frame[0] < 0.01,
          "near-ceiling: whole-frame fraction alone would have accepted it");
  }

  // Low signal: the center block is the normalizer, so a frame whose center
  // carries almost no signal cannot anchor a ratio.
  {
    const int width = 64;
    const int height = 64;
    const std::vector<double> mosaic(
        static_cast<std::size_t>(width) * height, 20.0);

    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             width, opts, kCeiling);

    check(!field.valid, "low signal: 2% of ceiling rejects");
    check(!field.gates.low_signal_ok, "low signal: gate verdict recorded");
    check_near(field.gates.center_signal_frac[0], 0.02, 1e-9,
               "low signal: center signal fraction reported");
  }

  // Negative residuals: black-subtracted samples may dip below zero on noise,
  // but a bright uniform field producing them in bulk means the pedestal or the
  // black source is wrong.
  {
    const int width = 64;
    const int height = 64;
    std::vector<double> mosaic(static_cast<std::size_t>(width) * height, 500.0);
    for (int i = 0; i < 200; ++i) mosaic[static_cast<std::size_t>(i)] = -3.0;

    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             width, opts, kCeiling);

    check(!field.valid, "negative residual: 4.9% negative rejects");
    check(!field.gates.negative_ok, "negative residual: gate verdict recorded");
  }

  // Bin coverage is not a restatement of the finiteness gate: it also catches a
  // grid finer than the plane, where bins have no samples to take a median of.
  {
    const int width = 16;
    const int height = 16;
    const std::vector<double> mosaic(
        static_cast<std::size_t>(width) * height, 500.0);

    ShadingOptions opts;
    opts.grid_cols = 32;
    opts.grid_rows = 32;
    opts.corner_block_px = 4;
    opts.corner_inset_px = 0;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             width, opts, kCeiling);

    check(!field.valid, "coverage: grid finer than the plane rejects");
    check(!field.gates.coverage_ok, "coverage: gate verdict recorded");
    check(field.gates.min_bin_coverage < 0.90,
          "coverage: worst-bin coverage reported");
  }

  // A clean frame passes every gate and says so, so an all-rejecting
  // implementation cannot pass the gate tests vacuously.
  {
    const int width = 128;
    const int height = 128;
    const std::vector<double> mosaic(
        static_cast<std::size_t>(width) * height, 500.0);

    ShadingOptions opts;
    opts.grid_cols = 4;
    opts.grid_rows = 4;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 0;
    const auto field = measure_shading_field(mosaic.data(), width, height,
                                             width, opts, kCeiling);

    check(field.valid, "clean frame: accepted");
    check(field.gates.near_ceiling_ok && field.gates.low_signal_ok &&
              field.gates.negative_ok && field.gates.coverage_ok &&
              field.gates.finite_ok,
          "clean frame: every gate verdict is pass");
    check_near(field.gates.center_signal_frac[0], 0.5, 1e-9,
               "clean frame: center signal fraction reported when passing");
    check_near(field.gates.min_bin_coverage, 1.0, 1e-9,
               "clean frame: full bin coverage reported when passing");
  }

  // The normalizer is the center block, never the gate region. The two are
  // different sizes on purpose: the gate has to see more of the frame than the
  // block it protects. This fixture makes them disagree — a 32x32 mosaic block
  // of 1000 inside a field of 500 — so normalizing by the wrong region is
  // visible as a factor of two rather than a rounding difference.
  {
    const int width = 128;
    const int height = 128;
    std::vector<double> mosaic(static_cast<std::size_t>(width) * height, 400.0);
    for (int y = 48; y < 80; ++y) {
      for (int x = 48; x < 80; ++x) {
        mosaic[static_cast<std::size_t>(y) * width + x] = 800.0;
      }
    }

    ShadingOptions opts;
    opts.grid_cols = 4;
    opts.grid_rows = 4;
    opts.corner_block_px = 32;    // plane block: 16x16 centred, all 1000
    opts.corner_inset_px = 0;
    opts.gate_center_frac = 0.50;  // plane region: 32x32 centred, median 500
    const auto field =
        measure_shading_field(mosaic.data(), width, height, width, opts, kCeiling);

    check(field.valid, "normalizer: measurement accepted");
    for (int p = 0; p < 4; ++p) {
      const std::string tag = "plane " + std::to_string(p);
      check_near(field.center_block_median[p], 800.0, 1e-9,
                 "normalizer: center block median is the block, not the gate "
                 "region " + tag);
      // Outer bins sit entirely in the 500 field. Block-normalized that is 0.5;
      // gate-region-normalized it would be 1.0.
      check_near(field.relative[p][0], 0.5, 1e-9,
                 "normalizer: corner bin normalized by the center block " + tag);
    }
  }

  // A non-positive normalizer is a denominator on every map and scalar. It must
  // reject, not divide: a dark or fully-clipped-to-zero frame otherwise emits
  // inf/NaN maps that look like measurements.
  {
    const int width = 64;
    const int height = 64;
    const std::vector<double> mosaic(
        static_cast<std::size_t>(width) * height, 0.0);

    ShadingOptions opts;
    opts.grid_cols = 2;
    opts.grid_rows = 2;
    opts.corner_block_px = 16;
    opts.corner_inset_px = 0;
    const auto field =
        measure_shading_field(mosaic.data(), width, height, width, opts, kCeiling);

    check(!field.valid, "zero normalizer: measurement rejected");
    for (int p = 0; p < 4; ++p) {
      check(field.relative[p].empty(),
            "zero normalizer: rejected maps are absent");
    }
    check(!field.rejection_reason.empty(),
          "zero normalizer: denominator failure is diagnostic");
  }

  // A constant field must report each plane's own constant and a relative
  // response of exactly 1.0 in every bin. Planes carry different constants so a
  // implementation that sums or averages the CFA positions cannot pass.
  {
    const int width = 128;
    const int height = 96;
    const std::array<double, 4> planes = {1000.0, 2000.0, 2000.0, 800.0};
    const auto mosaic = make_mosaic(width, height, planes);

    ShadingOptions opts;
    opts.grid_cols = 8;
    opts.grid_rows = 6;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 0;
    const auto field =
        measure_shading_field(mosaic.data(), width, height, width, opts,
                              kHeadroom);

    check(field.grid_cols == 8 && field.grid_rows == 6,
          "constant: grid geometry carried through");

    const std::size_t bins = 8 * 6;
    for (int p = 0; p < 4; ++p) {
      const std::string tag = "plane " + std::to_string(p);
      check(field.bin_median[p].size() == bins, "constant: bin count " + tag);
      check(field.relative[p].size() == bins, "constant: relative count " + tag);
      check_near(field.center_block_median[p], planes[p], 1e-9,
                 "constant: center median " + tag);

      // Seeded from non-emptiness so an empty result cannot pass vacuously.
      bool all_value = !field.bin_median[p].empty();
      bool all_unit = !field.relative[p].empty();
      for (std::size_t i = 0; i < field.bin_median[p].size(); ++i) {
        if (std::abs(field.bin_median[p][i] - planes[p]) > 1e-9) {
          all_value = false;
        }
        if (std::abs(field.relative[p][i] - 1.0) > 1e-12) all_unit = false;
      }
      check(all_value, "constant: every bin median equals its plane " + tag);
      check(all_unit, "constant: every relative response is 1.0 " + tag);
    }
  }

  // A known chromatic field must be recovered exactly. The two greens carry
  // different amplitudes but the same spatial field, so an implementation that
  // averages absolute green levels instead of center-normalized ones fails.
  // The matrices are asymmetric, so a row/column transposition also fails.
  {
    const Bins luminance = {{{0.50, 0.60, 0.70, 0.80},
                             {0.55, 1.00, 1.00, 0.85},
                             {0.65, 1.00, 1.00, 0.90},
                             {0.75, 0.95, 0.98, 0.99}}};
    const Bins red_factor = {{{0.980, 0.985, 0.990, 0.975},
                              {0.982, 1.000, 1.000, 0.978},
                              {0.984, 1.000, 1.000, 0.976},
                              {0.986, 0.988, 0.992, 0.974}}};
    const Bins blue_factor = {{{1.030, 1.035, 1.040, 1.025},
                               {1.032, 1.000, 1.000, 1.028},
                               {1.034, 1.000, 1.000, 1.026},
                               {1.036, 1.038, 1.042, 1.024}}};
    const Bins unity = {{{1.0, 1.0, 1.0, 1.0},
                         {1.0, 1.0, 1.0, 1.0},
                         {1.0, 1.0, 1.0, 1.0},
                         {1.0, 1.0, 1.0, 1.0}}};

    Bins red_field = luminance;
    Bins blue_field = luminance;
    for (int r = 0; r < kGrid; ++r) {
      for (int c = 0; c < kGrid; ++c) {
        red_field[r][c] *= red_factor[r][c];
        blue_field[r][c] *= blue_factor[r][c];
      }
    }

    const std::array<double, 4> amplitude = {1000.0, 2000.0, 2100.0, 800.0};
    const std::array<const Bins*, 4> fields = {&red_field, &luminance,
                                               &luminance, &blue_field};
    const auto mosaic = make_binned_mosaic(amplitude, fields);

    ShadingOptions opts;
    opts.grid_cols = kGrid;
    opts.grid_rows = kGrid;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 0;
    const auto field =
        measure_shading_field(mosaic.data(), kMosaic, kMosaic, kMosaic, opts,
                              kHeadroom);

    check(maps_match(field.relative[1], luminance, 1e-12),
          "chromatic: green plane relative response recovered");

    const auto chroma = chromatic_response(field, kRGGB, kCdesc);
    check(chroma.valid, "chromatic: RGGB layout accepted");
    check(maps_match(chroma.c_rg, red_factor, 1e-12), "chromatic: C_RG exact");
    check(maps_match(chroma.c_bg, blue_factor, 1e-12), "chromatic: C_BG exact");
    check(maps_match(chroma.c_g1g2, unity, 1e-12),
          "chromatic: C_G1G2 reads unity despite unequal green amplitudes");
  }

  // C_G1G2 is only a check if something can make it move. A uniform G1/G2 gain
  // difference cannot: center normalization divides it out, which the case
  // above pins. A *spatially varying* green mismatch is what this map exists to
  // catch, and it must be recovered as the reciprocal of the injected field.
  {
    const Bins luminance = {{{0.50, 0.60, 0.70, 0.80},
                             {0.55, 1.00, 1.00, 0.85},
                             {0.65, 1.00, 1.00, 0.90},
                             {0.75, 0.95, 0.98, 0.99}}};
    // Unity across the centre bins so the normalizer sees no mismatch and the
    // departure elsewhere is exactly the injected factor.
    const Bins g2_factor = {{{1.02, 1.01, 1.00, 0.99},
                             {1.01, 1.00, 1.00, 0.98},
                             {1.00, 1.00, 1.00, 0.97},
                             {0.99, 0.98, 0.97, 0.96}}};

    Bins g2_field = luminance;
    Bins expected = {};
    for (int r = 0; r < kGrid; ++r) {
      for (int c = 0; c < kGrid; ++c) {
        g2_field[r][c] *= g2_factor[r][c];
        expected[r][c] = 1.0 / g2_factor[r][c];
      }
    }

    const std::array<double, 4> amplitude = {1000.0, 2000.0, 2000.0, 800.0};
    const std::array<const Bins*, 4> fields = {&luminance, &luminance, &g2_field,
                                               &luminance};
    const auto mosaic = make_binned_mosaic(amplitude, fields);

    ShadingOptions opts;
    opts.grid_cols = kGrid;
    opts.grid_rows = kGrid;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 0;
    const auto field = measure_shading_field(mosaic.data(), kMosaic, kMosaic,
                                             kMosaic, opts, kHeadroom);
    const auto chroma = chromatic_response(field, kRGGB, kCdesc);

    check(chroma.valid, "green mismatch: layout accepted");
    check(maps_match(chroma.c_g1g2, expected, 1e-12),
          "green mismatch: C_G1G2 recovers a spatially varying green field");
    check(std::abs(chroma.c_g1g2[0] - 1.0) > 0.01,
          "green mismatch: C_G1G2 actually departs from unity");
  }

  // A dead bin makes C_G1G2 a division by zero. It must serialize as a missing
  // value, not as inf, and it must not silently poison the bins around it.
  {
    const Bins luminance = {{{1.00, 1.00, 1.00, 1.00},
                             {1.00, 1.00, 1.00, 1.00},
                             {1.00, 1.00, 1.00, 1.00},
                             {1.00, 1.00, 1.00, 1.00}}};
    Bins dead_corner = luminance;
    dead_corner[0][0] = 0.0;

    const std::array<double, 4> amplitude = {1000.0, 2000.0, 2000.0, 800.0};
    const std::array<const Bins*, 4> fields = {&luminance, &luminance,
                                               &dead_corner, &luminance};
    const auto mosaic = make_binned_mosaic(amplitude, fields);

    ShadingOptions opts;
    opts.grid_cols = kGrid;
    opts.grid_rows = kGrid;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 0;
    const auto field = measure_shading_field(mosaic.data(), kMosaic, kMosaic,
                                             kMosaic, opts, kHeadroom);
    const auto chroma = chromatic_response(field, kRGGB, kCdesc);

    check(chroma.valid, "dead bin: CFA mapping remains valid");
    check(!chroma.complete && chroma.missing_bin_count == 1,
          "dead bin: incompleteness is explicit and counted");
    check(std::isnan(chroma.c_g1g2[0]),
          "dead bin: C_G1G2 marks the undefined bin instead of emitting inf");
    bool rest_finite = true;
    for (std::size_t i = 1; i < chroma.c_g1g2.size(); ++i) {
      if (!std::isfinite(chroma.c_g1g2[i]) || !std::isfinite(chroma.c_rg[i]) ||
          !std::isfinite(chroma.c_bg[i])) {
        rest_finite = false;
      }
    }
    check(rest_finite, "dead bin: neighbouring bins are unaffected");
  }

  // A rejected field cannot be promoted into a chromatic result just because
  // vectors happen to be populated on a manually constructed object.
  {
    camera_iq::ShadingField rejected;
    rejected.grid_cols = 1;
    rejected.grid_rows = 1;
    for (auto& map : rejected.relative) map = {1.0};
    const auto chroma = chromatic_response(rejected, kRGGB, kCdesc);
    check(!chroma.valid && !chroma.complete,
          "chromatic: rejected field is refused");
  }

  // All four ordinary Bayer phases are mapped from the file's COLOR()/cdesc
  // metadata rather than an RGGB constant.
  {
    camera_iq::ShadingField field;
    field.valid = true;
    field.grid_cols = 1;
    field.grid_rows = 1;
    field.geometry.valid = true;
    for (int p = 0; p < 4; ++p) {
      field.relative[p] = {1.0};
      for (int q = 0; q < 4; ++q) field.blocks.corner_relative[q][p] = 1.0;
    }
    const std::array<std::array<int, 4>, 4> phases = {
        std::array<int, 4>{0, 1, 3, 2},  // RGGB
        std::array<int, 4>{1, 0, 2, 3},  // GRBG
        std::array<int, 4>{1, 2, 0, 3},  // GBRG
        std::array<int, 4>{2, 1, 3, 0},  // BGGR
    };
    for (const auto& phase : phases) {
      const auto chroma = chromatic_response(field, phase, kCdesc);
      check(chroma.valid && chroma.complete && chroma.red_position >= 0 &&
                chroma.blue_position >= 0 && chroma.green1_position >= 0 &&
                chroma.green2_position >= 0,
            "chromatic: ordinary Bayer phase mapped from metadata");
    }
  }

  // A dark is verified only after geometry/CFA, exposure metadata, and a
  // declared residual tolerance all pass. Measurement alone is not proof.
  {
    RawCfaImage primary;
    primary.width = primary.height = primary.row_stride_pixels = 16;
    primary.samples.assign(16 * 16, 500.0);
    primary.color_at_position = kRGGB;
    primary.cdesc = kCdesc;
    primary.meta.aperture = 8.0;
    primary.meta.shutter_s = 0.001;
    primary.meta.iso = 200.0;
    RawCfaImage dark = primary;
    dark.samples.assign(16 * 16, 0.25);

    const auto verified =
        verify_shading_pedestal(primary, dark, "dark.RAF", 1.0);
    check(verified.measured && verified.compatible &&
              verified.exposure_metadata_matches && verified.within_tolerance &&
              verified.verified,
          "pedestal: all evidence gates produce a verified result");

    dark.meta.shutter_s = 0.002;
    const auto mismatch =
        verify_shading_pedestal(primary, dark, "dark.RAF", 1.0);
    check(mismatch.measured && !mismatch.exposure_metadata_matches &&
              !mismatch.verified,
          "pedestal: measured but mismatched dark remains unverified");
  }

  // Quadrant asymmetry is defined so that A = 0 for any centred, radially
  // symmetric field, by construction. That is the whole point of the statistic:
  // a lens-only, centred explanation cannot produce a non-zero A, so a non-zero
  // A is evidence the field is not centred and radially symmetric.
  {
    const int width = 256;
    const int height = 256;
    std::vector<double> radial(static_cast<std::size_t>(width) * height, 0.0);
    std::vector<double> tilted(radial.size(), 0.0);
    const double cx = (width - 1) / 2.0;
    const double cy = (height - 1) / 2.0;
    const double rmax = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const double dx = x - cx;
        const double dy = y - cy;
        const double r = std::sqrt(dx * dx + dy * dy) / rmax;
        const double base = 1000.0 * (1.0 - 0.3 * r * r);
        const std::size_t i = static_cast<std::size_t>(y) * width + x;
        const int p = (y % 2) * 2 + (x % 2);
        const bool green = (p == 1 || p == 2);  // RGGB
        radial[i] = base;
        // Same field with a horizontal gradient laid over it: still smooth,
        // still falls off from the centre, but no longer centred. The red and
        // blue planes carry the opposite gradient, so A computed on the wrong
        // planes comes out with the wrong sign rather than merely the wrong
        // magnitude.
        tilted[i] = base * (1.0 + (green ? 0.20 : -0.20) * (dx / cx));
      }
    }

    ShadingOptions opts;
    opts.grid_cols = 4;
    opts.grid_rows = 4;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 32;

    const auto sym = measure_shading_field(radial.data(), width, height, width,
                                           opts, kHeadroom);
    const auto sym_c = chromatic_response(sym, kRGGB, kCdesc, opts);
    check(sym.valid, "asymmetry: radial field measured");
    // Analytically A = 0 here. On a discrete CFA the two green positions sample
    // half a pixel apart, so the floor is ~1.5e-4 — two orders below the 0.05
    // policy and below the 0.0025 reproducibility measured on the archive
    // repeat pair. The tolerance records the sampling floor, not slack.
    check(sym_c.green_asymmetry < 1e-3,
          "asymmetry: A is zero for a centred radially symmetric field");
    check(!sym_c.asymmetry_exceeds_policy,
          "asymmetry: centred field does not trip the policy");

    const auto asym = measure_shading_field(tilted.data(), width, height, width,
                                            opts, kHeadroom);
    const auto asym_c = chromatic_response(asym, kRGGB, kCdesc, opts);
    check(asym.valid, "asymmetry: tilted field measured");
    check(asym_c.green_asymmetry > 0.05,
          "asymmetry: an off-centre field trips the 0.05 policy");
    check(asym_c.asymmetry_exceeds_policy,
          "asymmetry: policy verdict recorded alongside the value");

    // The four corner blocks are distinct measurements, not one number: the
    // tilt has to show up as left-vs-right, not as a scalar with no direction.
    const double tl = asym.blocks.corner_relative[0][1];
    const double tr = asym.blocks.corner_relative[1][1];
    // Red carries the opposite gradient, so a green-plane A cannot be the same
    // number as a red-plane one.
    check(asym.blocks.corner_relative[1][0] < asym.blocks.corner_relative[0][0],
          "asymmetry: red plane carries the opposite gradient");
    check(tr > tl,
          "asymmetry: corner blocks carry the direction of the gradient");
  }

  // Repeatability / exposure invariance. The statistic is defined per corner
  // site and per plane as the absolute difference in relative response in
  // percentage points, reported as both the maximum and the RMS — one frame
  // pair supports a check, not a proof, so both numbers travel together.
  {
    const int width = 128;
    const int height = 128;
    std::vector<double> a(static_cast<std::size_t>(width) * height, 0.0);
    std::vector<double> b(a.size(), 0.0);
    const double cx = (width - 1) / 2.0, cy = (height - 1) / 2.0;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const double dx = (x - cx) / cx, dy = (y - cy) / cy;
        const double r2 = dx * dx + dy * dy;
        const std::size_t i = static_cast<std::size_t>(y) * width + x;
        a[i] = 1000.0 * (1.0 - 0.20 * r2);
        // Same field at half the exposure. A purely multiplicative field is
        // exposure-invariant once each frame is normalized by its own centre,
        // so the corner deltas must be zero — that is what makes a non-zero
        // delta on real frames evidence rather than arithmetic.
        b[i] = 0.5 * a[i];
      }
    }

    ShadingOptions opts;
    opts.grid_cols = 4;
    opts.grid_rows = 4;
    opts.corner_block_px = 32;
    opts.corner_inset_px = 16;
    const auto fa = measure_shading_field(a.data(), width, height, width, opts,
                                          kHeadroom);
    const auto fb = measure_shading_field(b.data(), width, height, width, opts,
                                          kHeadroom);
    check(fa.valid && fb.valid, "compare: both frames measured");

    const auto same = camera_iq::compare_shading_fields(fa, fa);
    check(same.measured, "compare: a frame against itself is a measurement");
    check_near(same.max_corner_delta_pp, 0.0, 1e-12,
               "compare: identical frames differ by zero");

    const auto scaled = camera_iq::compare_shading_fields(fa, fb);
    check_near(scaled.max_corner_delta_pp, 0.0, 1e-9,
               "compare: a multiplicative exposure change cancels under "
               "center normalization");

    // An additive offset does not cancel, which is the failure mode the check
    // exists to expose.
    std::vector<double> c = a;
    for (auto& v : c) v += 100.0;
    const auto fc = measure_shading_field(c.data(), width, height, width, opts,
                                          kHeadroom);
    const auto offset = camera_iq::compare_shading_fields(fa, fc);
    check(offset.max_corner_delta_pp > 0.1,
          "compare: an additive pedestal offset shows up as a corner delta");
    check(offset.rms_corner_delta_pp > 0.0 &&
              offset.rms_corner_delta_pp <= offset.max_corner_delta_pp,
          "compare: RMS reported alongside the maximum and bounded by it");
  }
}
