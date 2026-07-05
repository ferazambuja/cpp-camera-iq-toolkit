#include "camera_iq/chart_localization.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "harness.hpp"

using camera_iq::ChartCorners;
using camera_iq::PatchCoord;
using camera_iq::Point2d;
using camera_iq::localize_colorchecker_sg_grid;
using test::check;
using test::check_near;

namespace {

Point2d center_zero_based(const PatchCoord& coord) {
  return {coord.x - 1.0 + coord.width / 2.0,
          coord.y - 1.0 + coord.height / 2.0};
}

const camera_iq::ChartPatchGeometry& patch_at(
    const camera_iq::ChartLocalizationResult& result, int row, int column) {
  return result.patches[static_cast<std::size_t>(row * 14 + column)];
}

bool throws_for(const ChartCorners& corners, double inner_fraction = 0.65) {
  try {
    (void)localize_colorchecker_sg_grid(corners, inner_fraction);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

void TESTS() {
  {
    const ChartCorners corners{{0, 0}, {242, 0}, {242, 172}, {0, 172}};
    const auto result = localize_colorchecker_sg_grid(corners);

    check(result.patches.size() == 140,
          "identity grid: ColorChecker-SG has 140 patches");
    check(result.patches.front().reference_patch_id == "A1",
          "identity grid: first patch id is A1");
    check(result.patches.back().reference_patch_id == "N10",
          "identity grid: last patch id is N10");
    check(result.patches.front().row == 0 && result.patches.front().column == 0,
          "identity grid: A1 is row 0 column 0");
    check(result.patches.back().row == 9 && result.patches.back().column == 13,
          "identity grid: N10 is row 9 column 13");
    check(patch_at(result, 0, 1).reference_patch_id == "B1",
          "identity grid: row-major advances columns first");
    check(patch_at(result, 1, 0).reference_patch_id == "A2",
          "identity grid: row-major advances rows after N1");

    const auto& a1 = result.patches.front().extraction_coord;
    check_near(a1.x, 3.5375, 1e-9,
               "identity grid: A1 uses physical SG patch left");
    check_near(a1.y, 3.5375, 1e-9,
               "identity grid: A1 uses physical SG patch top");
    check_near(a1.width, 9.425, 1e-9,
               "identity grid: A1 central ROI width");
    check_near(a1.height, 9.425, 1e-9,
               "identity grid: A1 central ROI height");

    const auto& n10 = result.patches.back().extraction_coord;
    check_near(n10.x, 231.0375, 1e-9,
               "identity grid: N10 uses physical SG patch left");
    check_near(n10.y, 161.0375, 1e-9,
               "identity grid: N10 uses physical SG patch top");
    check_near(n10.width, 9.425, 1e-9,
               "identity grid: N10 central ROI width");
    check_near(n10.height, 9.425, 1e-9,
               "identity grid: N10 central ROI height");

    bool all_inside = true;
    for (const auto& patch : result.patches) {
      const auto& coord = patch.extraction_coord;
      if (coord.x < 1.0 || coord.y < 1.0 ||
          coord.x + coord.width - 1.0 > 242.0 ||
          coord.y + coord.height - 1.0 > 172.0) {
        all_inside = false;
      }
    }
    check(all_inside,
          "identity grid: generated rectangles stay inside outer chart bounds");
  }

  {
    const ChartCorners corners{{20, 10}, {260, 0}, {245, 190}, {5, 170}};
    const auto result = localize_colorchecker_sg_grid(corners);
    bool rows_increase_left_to_right = true;
    bool columns_increase_top_to_bottom = true;
    for (int row = 0; row < 10; ++row) {
      for (int column = 1; column < 14; ++column) {
        const auto prev =
            center_zero_based(patch_at(result, row, column - 1).extraction_coord);
        const auto curr =
            center_zero_based(patch_at(result, row, column).extraction_coord);
        if (!(prev.x < curr.x)) {
          rows_increase_left_to_right = false;
        }
      }
    }
    for (int column = 0; column < 14; ++column) {
      for (int row = 1; row < 10; ++row) {
        const auto prev =
            center_zero_based(patch_at(result, row - 1, column).extraction_coord);
        const auto curr =
            center_zero_based(patch_at(result, row, column).extraction_coord);
        if (!(prev.y < curr.y)) {
          columns_increase_top_to_bottom = false;
        }
      }
    }
    check(rows_increase_left_to_right,
          "perspective grid: row order is preserved left to right");
    check(columns_increase_top_to_bottom,
          "perspective grid: column order is preserved top to bottom");
  }

  {
    check(throws_for(ChartCorners{{0, 0}, {0, 0}, {10, 10}, {0, 10}}),
          "invalid grid: zero-area top edge rejected");
    check(throws_for(ChartCorners{{0, 0}, {100, 100}, {100, 0}, {0, 100}}),
          "invalid grid: crossed polygon rejected");
    check(throws_for(ChartCorners{{0, 0},
                                  {std::numeric_limits<double>::quiet_NaN(), 0},
                                  {10, 10},
                                  {0, 10}}),
          "invalid grid: non-finite corner rejected");
    check(throws_for(ChartCorners{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 0.0),
          "invalid grid: zero inner fraction rejected");
    check(throws_for(ChartCorners{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 1.1),
          "invalid grid: oversized inner fraction rejected");
  }
}
