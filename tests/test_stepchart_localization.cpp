#include "camera_iq/stepchart_localization.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "harness.hpp"

using camera_iq::ChartCorners;
using camera_iq::Point2d;
using camera_iq::StepchartZoneGeometry;
using camera_iq::localize_stepchart_20_zone_strip;
using camera_iq::parse_stepchart_strip_corners;
using test::check;
using test::check_near;

namespace {

Point2d center_zero_based(const camera_iq::PatchCoord& coord) {
  return {coord.x - 1.0 + coord.width / 2.0,
          coord.y - 1.0 + coord.height / 2.0};
}

const StepchartZoneGeometry& zone_at(
    const camera_iq::StepchartLocalizationResult& result, int zone) {
  return result.zones[static_cast<std::size_t>(zone - 1)];
}

bool throws_for(const ChartCorners& corners, double inner_fraction = 0.65) {
  try {
    (void)localize_stepchart_20_zone_strip(corners, inner_fraction);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

bool parse_throws_for(const std::string& text) {
  try {
    (void)parse_stepchart_strip_corners(text);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

void TESTS() {
  {
    const ChartCorners corners{{0, 0}, {200, 0}, {200, 100}, {0, 100}};
    const auto result = localize_stepchart_20_zone_strip(corners);

    check(result.zones.size() == 20,
          "identity strip: OECF Stepchart has 20 zones");
    check(result.zones.front().zone == 1,
          "identity strip: first zone is 1");
    check(result.zones.back().zone == 20,
          "identity strip: last zone is 20");
    check(result.zones.front().row == 0 &&
              result.zones.front().column == 0,
          "identity strip: zone 1 is row 0 column 0");
    check(result.zones.back().row == 0 && result.zones.back().column == 19,
          "identity strip: zone 20 is row 0 column 19");

    const auto& z1 = result.zones.front().extraction_coord;
    check_near(z1.x, 2.75, 1e-9,
               "identity strip: zone 1 central ROI x is one-based");
    check_near(z1.y, 18.5, 1e-9,
               "identity strip: zone 1 central ROI y is one-based");
    check_near(z1.width, 6.5, 1e-9,
               "identity strip: zone 1 central ROI width");
    check_near(z1.height, 65.0, 1e-9,
               "identity strip: zone 1 central ROI height");

    const auto& z20 = result.zones.back().extraction_coord;
    check_near(z20.x, 192.75, 1e-9,
               "identity strip: zone 20 central ROI x is one-based");
    check_near(z20.width, 6.5, 1e-9,
               "identity strip: zone 20 central ROI width");

    bool all_inside = true;
    bool ordered = true;
    for (const auto& zone : result.zones) {
      const auto& coord = zone.extraction_coord;
      if (coord.x < 1.0 || coord.y < 1.0 ||
          coord.x + coord.width - 1.0 > 200.0 ||
          coord.y + coord.height - 1.0 > 100.0) {
        all_inside = false;
      }
      if (zone.zone > 1) {
        const auto prev =
            center_zero_based(zone_at(result, zone.zone - 1).extraction_coord);
        const auto curr = center_zero_based(zone.extraction_coord);
        if (!(prev.x < curr.x)) {
          ordered = false;
        }
      }
    }
    check(all_inside,
          "identity strip: generated zone rectangles stay inside strip bounds");
    check(ordered,
          "identity strip: generated zone centers increase left to right");
  }

  {
    const ChartCorners corners{{20, 10}, {230, 0}, {210, 120}, {5, 100}};
    const auto result = localize_stepchart_20_zone_strip(corners, 0.01);
    bool ordered = true;
    for (int zone = 2; zone <= 20; ++zone) {
      const auto prev =
          center_zero_based(zone_at(result, zone - 1).extraction_coord);
      const auto curr = center_zero_based(zone_at(result, zone).extraction_coord);
      if (!(prev.x < curr.x)) {
        ordered = false;
      }
    }
    check(ordered,
          "perspective strip: zone order is preserved left to right");

    const Point2d p1 = center_zero_based(zone_at(result, 1).extraction_coord);
    const Point2d p20 = center_zero_based(zone_at(result, 20).extraction_coord);
    const double dx = p20.x - p1.x;
    const double dy = p20.y - p1.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    double max_offline = 0.0;
    for (int zone = 2; zone < 20; ++zone) {
      const Point2d p =
          center_zero_based(zone_at(result, zone).extraction_coord);
      const double dist =
          std::abs(dx * (p.y - p1.y) - dy * (p.x - p1.x)) / len;
      if (dist > max_offline) max_offline = dist;
    }
    check(max_offline < 0.1,
          "perspective strip: zone centers stay on the strip midline");
  }

  {
    const auto parsed =
        parse_stepchart_strip_corners("0,0;200,0;200,100;0,100");
    const auto result = localize_stepchart_20_zone_strip(parsed);
    check(result.zones.size() == 20,
          "stepchart corners: parsed corners generate 20 zones");
    check(parse_throws_for("0,0;200,0;200,100"),
          "stepchart corners: rejects too few corners");
    check(parse_throws_for("0,0;200,0;bad;0,100"),
          "stepchart corners: rejects malformed point");
    check(parse_throws_for("0,0;200,0;200,100;0,100;9,9"),
          "stepchart corners: rejects extra corners");
  }

  {
    check(throws_for(ChartCorners{{0, 0}, {0, 0}, {10, 10}, {0, 10}}),
          "invalid strip: zero-area top edge rejected");
    check(throws_for(ChartCorners{{0, 0}, {100, 100}, {100, 0}, {0, 100}}),
          "invalid strip: crossed polygon rejected");
    check(throws_for(ChartCorners{{0, 0},
                                  {std::numeric_limits<double>::quiet_NaN(), 0},
                                  {10, 10},
                                  {0, 10}}),
          "invalid strip: non-finite corner rejected");
    check(throws_for(ChartCorners{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 0.0),
          "invalid strip: zero inner fraction rejected");
    check(throws_for(ChartCorners{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, 1.1),
          "invalid strip: oversized inner fraction rejected");
  }
}
