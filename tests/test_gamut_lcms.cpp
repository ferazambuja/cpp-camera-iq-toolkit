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

// LittleCMS carries this transform through a float32 pipeline, so exact
// agreement is not available and the tolerance has to be set from the observed
// disagreement rather than from machine epsilon. Measured worst case over every
// sample below, both directions, is 2.8e-8; 1e-6 keeps roughly a factor of 36
// in reserve for library-version and platform variation. The looser 3e-5 this
// replaces admitted a primary-matrix error of about 5e-5 relative without
// failing, which is large enough to move a published CIEDE2000 figure.
constexpr double kReferenceTolerance = 1e-6;

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

EncodedRgb lcms_convert(const EncodedRgb& input, bool source_is_p3) {
  const Profile source = make_profile(source_is_p3);
  const Profile destination = make_profile(!source_is_p3);
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
    const auto reference = lcms_convert(input, true);
    check_near(ours.output_encoded.r, reference.r, kReferenceTolerance,
               "LittleCMS cross-check: encoded red");
    check_near(ours.output_encoded.g, reference.g, kReferenceTolerance,
               "LittleCMS cross-check: encoded green");
    check_near(ours.output_encoded.b, reference.b, kReferenceTolerance,
               "LittleCMS cross-check: encoded blue");
  }

  // The Display-P3 arm above can only use common-gamut colors, so every one of
  // its samples is desaturated: the saturated P3 corners that produce the
  // published CIEDE2000 maxima are out of sRGB and have no reference to compare
  // against. Running the conversion the other way fixes that. The sRGB
  // primaries and secondaries all lie inside Display-P3, so the full-saturation
  // corners stay common-gamut and the primary matrices are exercised at the
  // extremes where a transposed row or a swapped primary would show up, rather
  // than only near the neutral axis where every plausible matrix agrees.
  GamutMapOptions into_p3;
  into_p3.source = RgbColorSpace::Srgb;
  into_p3.destination = RgbColorSpace::DisplayP3;
  into_p3.intent = GamutMapIntent::BoundaryProjection;

  const std::array<EncodedRgb, 9> srgb_corners = {{
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0},
      {0.0, 0.0, 1.0},
      {1.0, 1.0, 0.0},
      {0.0, 1.0, 1.0},
      {1.0, 0.0, 1.0},
      {1.0, 1.0, 1.0},
      {0.0, 0.0, 0.0},
      {0.02, 0.0, 0.0},
  }};
  for (const auto& input : srgb_corners) {
    const auto ours = map_encoded_rgb_to_gamut(input, into_p3);
    check(ours.input_in_destination,
          "LittleCMS cross-check: sRGB corner is inside Display-P3");
    check(!ours.modified,
          "LittleCMS cross-check: in-gamut corner is a colorimetric identity");
    const auto reference = lcms_convert(input, false);
    check_near(ours.output_encoded.r, reference.r, kReferenceTolerance,
               "LittleCMS cross-check: sRGB-to-P3 encoded red");
    check_near(ours.output_encoded.g, reference.g, kReferenceTolerance,
               "LittleCMS cross-check: sRGB-to-P3 encoded green");
    check_near(ours.output_encoded.b, reference.b, kReferenceTolerance,
               "LittleCMS cross-check: sRGB-to-P3 encoded blue");
  }
}
