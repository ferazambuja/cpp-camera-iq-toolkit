#include "camera_iq/stepchart_localization.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "harness.hpp"

using camera_iq::ChartCorners;
using camera_iq::Point2d;
using camera_iq::StepchartZoneGeometry;
using camera_iq::StepchartRingSeed;
using camera_iq::localize_stepchart_20_zone_ring;
using camera_iq::localize_stepchart_20_zone_strip;
using camera_iq::parse_stepchart_ring_seed;
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

bool ring_parse_throws_for(const std::string& text) {
  try {
    (void)parse_stepchart_ring_seed(text);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

bool ring_throws_for(const StepchartRingSeed& seed, double roi_size_px = 150.0) {
  try {
    (void)localize_stepchart_20_zone_ring(seed, roi_size_px);
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
    const StepchartRingSeed seed{{200, 200}, 100, -90};
    const auto result = localize_stepchart_20_zone_ring(seed, 20);

    check(result.chart_model == "OECF Stepchart 20-zone ring",
          "identity ring: chart model names ring layout");
    check(result.method == "ring_seeded_iso14524_style_alternating",
          "identity ring: method names alternating ring formula");
    check(result.ring_seed.has_value(), "identity ring: seed is preserved");
    check_near(result.roi_size_px, 20.0, 1e-12,
               "identity ring: ROI size is preserved");
    check(result.zones.size() == 20,
          "identity ring: OECF Stepchart has 20 zones");
    check(result.zones.front().zone == 1,
          "identity ring: first zone is 1");
    check(result.zones.back().zone == 20,
          "identity ring: last zone is 20");

    const auto z1 = center_zero_based(zone_at(result, 1).extraction_coord);
    check_near(z1.x, 200.0, 1e-9,
               "identity ring: zone 1 sits at theta1 x");
    check_near(z1.y, 100.0, 1e-9,
               "identity ring: zone 1 sits at theta1 y");
    check_near(zone_at(result, 1).extraction_coord.width, 20.0, 1e-9,
               "identity ring: zone ROI width");
    check_near(zone_at(result, 1).extraction_coord.height, 20.0, 1e-9,
               "identity ring: zone ROI height");

    const auto z2 = center_zero_based(zone_at(result, 2).extraction_coord);
    const auto z3 = center_zero_based(zone_at(result, 3).extraction_coord);
    check(z2.x > z1.x && z3.x < z1.x,
          "identity ring: zones alternate right then left after top patch");
    check_near(z2.y, z3.y, 1e-9,
               "identity ring: alternating pair has symmetric y");
  }

  {
    const StepchartRingSeed d800{{3633, 2582}, 1341, -97.8};
    const auto result = localize_stepchart_20_zone_ring(d800, 150);
    const auto z12 = center_zero_based(zone_at(result, 12).extraction_coord);
    const auto z13 = center_zero_based(zone_at(result, 13).extraction_coord);
    const auto z14 = center_zero_based(zone_at(result, 14).extraction_coord);

    check_near(z12.x, 4952.0, 1.0,
               "D800 ring: zone 12 center follows +10.2 degree slot");
    check_near(z12.y, 2819.0, 1.0,
               "D800 ring: zone 12 center y");
    check_near(z13.x, 2425.7, 1.0,
               "D800 ring: zone 13 center follows -205.8 degree slot");
    check_near(z13.y, 3165.6, 1.0,
               "D800 ring: zone 13 center y");
    check_near(z14.x, 4814.0, 1.0,
               "D800 ring: zone 14 center follows +28.2 degree slot");
    check_near(z14.y, 3215.7, 1.0,
               "D800 ring: zone 14 center y");
  }

  {
    const auto seed = parse_stepchart_ring_seed("3633,2582,1341,-97.8");
    check_near(seed.center.x, 3633.0, 1e-12,
               "stepchart ring seed: parses center x");
    check_near(seed.center.y, 2582.0, 1e-12,
               "stepchart ring seed: parses center y");
    check_near(seed.radius, 1341.0, 1e-12,
               "stepchart ring seed: parses radius");
    check_near(seed.theta1_degrees, -97.8, 1e-12,
               "stepchart ring seed: parses theta1");
    check(ring_parse_throws_for("3633,2582,1341"),
          "stepchart ring seed: rejects too few numbers");
    check(ring_parse_throws_for("3633,2582,0,-97.8"),
          "stepchart ring seed: rejects zero radius");
    check(ring_parse_throws_for("3633,2582,1341,bad"),
          "stepchart ring seed: rejects malformed theta");
    check(ring_parse_throws_for("3633,2582,1341,-97.8,5"),
          "stepchart ring seed: rejects too many numbers");
    check(ring_parse_throws_for("40000000,2582,1341,-97.8"),
          "stepchart ring seed: rejects implausibly large center");
    check(ring_parse_throws_for("3633,2582,20000000,-97.8"),
          "stepchart ring seed: rejects implausibly large radius");
    check(ring_throws_for(StepchartRingSeed{{3633, 2582}, 1341, -97.8}, 0.0),
          "stepchart ring seed: rejects zero ROI size");
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
