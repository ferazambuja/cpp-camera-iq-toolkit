#include "camera_iq/gamut_mapping.hpp"

#include <array>
#include <memory>
#include <stdexcept>

#include <lcms2.h>

#include "harness.hpp"

using camera_iq::EncodedRgb;
using camera_iq::GamutMapIntent;
using camera_iq::GamutMapOptions;
using camera_iq::RgbColorSpace;
using camera_iq::map_encoded_rgb_to_gamut;
using test::check;
using test::check_near;

namespace {

struct ProfileCloser {
  void operator()(void* profile) const {
    if (profile) cmsCloseProfile(static_cast<cmsHPROFILE>(profile));
  }
};

struct CurveCloser {
  void operator()(cmsToneCurve* curve) const {
    if (curve) cmsFreeToneCurve(curve);
  }
};

using Profile = std::unique_ptr<void, ProfileCloser>;
using Curve = std::unique_ptr<cmsToneCurve, CurveCloser>;

Profile make_profile(bool display_p3) {
  cmsCIExyY white{0.3127, 0.3290, 1.0};
  cmsCIExyYTRIPLE primaries{};
  if (display_p3) {
    primaries.Red = {0.680, 0.320, 1.0};
    primaries.Green = {0.265, 0.690, 1.0};
    primaries.Blue = {0.150, 0.060, 1.0};
  } else {
    primaries.Red = {0.640, 0.330, 1.0};
    primaries.Green = {0.300, 0.600, 1.0};
    primaries.Blue = {0.150, 0.060, 1.0};
  }
  // LittleCMS parametric curve type 4 is
  // (aX+b)^g+e above d, cX+f below d: the standard sRGB EOTF.
  double parameters[7] = {2.4, 1.0 / 1.055, 0.055 / 1.055,
                          1.0 / 12.92, 0.04045, 0.0, 0.0};
  Curve red(cmsBuildParametricToneCurve(nullptr, 4, parameters));
  Curve green(cmsBuildParametricToneCurve(nullptr, 4, parameters));
  Curve blue(cmsBuildParametricToneCurve(nullptr, 4, parameters));
  if (!red || !green || !blue) {
    throw std::runtime_error("LittleCMS: cannot create sRGB tone curves");
  }
  cmsToneCurve* curves[3] = {red.get(), green.get(), blue.get()};
  Profile profile(cmsCreateRGBProfile(&white, &primaries, curves));
  if (!profile) throw std::runtime_error("LittleCMS: cannot create RGB profile");
  return profile;
}

EncodedRgb lcms_p3_to_srgb(const EncodedRgb& input) {
  const Profile source = make_profile(true);
  const Profile destination = make_profile(false);
  const cmsHTRANSFORM transform = cmsCreateTransform(
      source.get(), TYPE_RGB_DBL, destination.get(), TYPE_RGB_DBL,
      INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);
  if (!transform) throw std::runtime_error("LittleCMS: cannot create transform");
  const std::array<double, 3> in = {input.r, input.g, input.b};
  std::array<double, 3> out{};
  cmsDoTransform(transform, in.data(), out.data(), 1);
  cmsDeleteTransform(transform);
  return {out[0], out[1], out[2]};
}

}  // namespace

void TESTS() {
  GamutMapOptions options;
  options.source = RgbColorSpace::DisplayP3;
  options.destination = RgbColorSpace::Srgb;
  options.intent = GamutMapIntent::BoundaryProjection;

  const std::array<EncodedRgb, 4> common_gamut = {{
      {0.5, 0.5, 0.5},
      {0.35, 0.40, 0.45},
      {0.60, 0.50, 0.40},
      {0.91748755732516563, 0.20028680774084706,
       0.13856059121111408},
  }};
  for (const auto& input : common_gamut) {
    const auto ours = map_encoded_rgb_to_gamut(input, options);
    check(ours.input_in_destination,
          "LittleCMS cross-check: selected color is common-gamut");
    const auto reference = lcms_p3_to_srgb(input);
    check_near(ours.output_encoded.r, reference.r, 3e-5,
               "LittleCMS cross-check: encoded red");
    check_near(ours.output_encoded.g, reference.g, 3e-5,
               "LittleCMS cross-check: encoded green");
    check_near(ours.output_encoded.b, reference.b, 3e-5,
               "LittleCMS cross-check: encoded blue");
  }

}
