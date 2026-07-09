#include "camera_iq/stepchart_raw.hpp"

#include <stdexcept>
#include <vector>

#include "harness.hpp"

using camera_iq::ChannelStats;
using camera_iq::ImatestStepchartZone;
using camera_iq::RoiRect;
using camera_iq::StepchartRawFrameMeasurement;
using camera_iq::StepchartRawZoneMeasurement;
using camera_iq::summarize_stepchart_raw_iso;
using test::check;
using test::check_near;

namespace {

ChannelStats plane(const char* label, double mean, double stddev,
                   double below = 0.0, double saturated = 0.0) {
  ChannelStats p;
  p.label = label;
  p.count = 100;
  p.mean = mean;
  p.stddev = stddev;
  p.below_black_fraction = below;
  p.saturated_fraction = saturated;
  return p;
}

StepchartRawZoneMeasurement zone_measurement(int zone, double base) {
  StepchartRawZoneMeasurement z;
  z.zone = zone;
  z.measurement_roi = RoiRect{10 * zone, 20, 8, 6};
  z.planes = {plane("R", base + 1.0, 1.0, 0.1, 0.0),
              plane("G1", base + 2.0, 2.0, 0.0, 0.2),
              plane("B", base + 3.0, 3.0, 0.0, 0.0),
              plane("G2", base + 4.0, 4.0, 0.0, 0.0)};
  return z;
}

StepchartRawFrameMeasurement frame(const std::string& file, double zone1_base,
                                   double zone2_base) {
  StepchartRawFrameMeasurement f;
  f.file_label = file;
  f.zones = {zone_measurement(1, zone1_base),
             zone_measurement(2, zone2_base)};
  return f;
}

std::vector<ImatestStepchartZone> oracle_zones() {
  ImatestStepchartZone z1;
  z1.zone = 1;
  z1.log_exposure = -0.0;
  ImatestStepchartZone z2;
  z2.zone = 2;
  z2.log_exposure = -0.15;
  return {z1, z2};
}

bool throws_for(const std::vector<StepchartRawFrameMeasurement>& frames) {
  try {
    (void)summarize_stepchart_raw_iso(100, "s1-40", oracle_zones(), frames);
  } catch (const std::runtime_error&) {
    return true;
  }
  return false;
}

}  // namespace

void TESTS() {
  {
    const auto summary = summarize_stepchart_raw_iso(
        100, "s1-40", oracle_zones(),
        {frame("a.NEF", 10.0, 100.0), frame("b.NEF", 14.0, 104.0)});

    check(summary.iso == 100, "raw stepchart: preserves ISO");
    check(summary.shutter_token == "s1-40",
          "raw stepchart: preserves shutter token");
    check(summary.frame_count == 2, "raw stepchart: frame count");
    check(summary.zones.size() == 2, "raw stepchart: zone count");
    check(summary.zones[0].zone == 1 && summary.zones[1].zone == 2,
          "raw stepchart: zones remain ordered by oracle");
    check_near(summary.zones[1].log_exposure, -0.15, 1e-12,
               "raw stepchart: carries oracle log exposure");
    check(summary.zones[0].measurement_roi.has_value(),
          "raw stepchart: repeated identical actual ROI is serialized");
    check(summary.zones[0].measurement_roi->x == 10,
          "raw stepchart: ROI x is preserved");

    const auto& r = summary.zones[0].planes[0];
    check(r.channel == "R", "raw stepchart: plane label preserved");
    check(r.frame_count == 2, "raw stepchart: plane frame count");
    check_near(r.mean_dn, 13.0, 1e-12,
               "raw stepchart: mean DN is average of repeat ROI means");
    check_near(r.temporal_stddev_of_zone_mean_dn, 2.0, 1e-12,
               "raw stepchart: repeat spread is population stddev of ROI means");
    check_near(r.mean_spatial_stddev_dn, 1.0, 1e-12,
               "raw stepchart: spatial stddev averaged across repeats");
    check_near(r.max_below_black_fraction, 0.1, 1e-12,
               "raw stepchart: below-black fraction rollup");
    check_near(summary.zones[0].planes[1].max_saturated_fraction, 0.2, 1e-12,
               "raw stepchart: saturation fraction rollup");

    check(!summary.ptc_candidate,
          "raw stepchart: repeat ROI spread is not promoted to PTC");
    check(!summary.dynamic_range_candidate,
          "raw stepchart: no engineering DR claim");
    check(!summary.limitations.empty(),
          "raw stepchart: limitations explain DN-only scope");
  }

  {
    StepchartRawFrameMeasurement missing = frame("a.NEF", 10.0, 100.0);
    missing.zones.pop_back();
    check(throws_for({missing, frame("b.NEF", 14.0, 104.0)}),
          "raw stepchart: rejects frames missing an oracle zone");
  }

  {
    auto duplicate = frame("a.NEF", 10.0, 100.0);
    duplicate.zones.push_back(zone_measurement(2, 200.0));
    check(throws_for({duplicate, frame("b.NEF", 14.0, 104.0)}),
          "raw stepchart: rejects duplicate zones in a frame");
  }

  {
    auto changed_roi_a = frame("a.NEF", 10.0, 100.0);
    auto changed_roi_b = frame("b.NEF", 14.0, 104.0);
    changed_roi_b.zones[0].measurement_roi = RoiRect{99, 20, 8, 6};
    const auto summary = summarize_stepchart_raw_iso(
        100, "s1-40", oracle_zones(), {changed_roi_a, changed_roi_b});
    check(!summary.zones[0].measurement_roi.has_value(),
          "raw stepchart: varying clipped ROI is marked null");
  }
}
