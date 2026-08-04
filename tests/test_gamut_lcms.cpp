#include "camera_iq/gamut_mapping.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

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

// LittleCMS evaluates the transform through a float path, so exact agreement
// with the toolkit's double-precision path is not expected. This is the
// inter-implementation contract enforced on every CI platform; it is not a
// claim that different LittleCMS versions produce identical rounding.
constexpr double kReferenceTolerance = 1e-6;

struct ReferenceSample {
  const char* name;
  EncodedRgb input;
};

std::string check_name(const ReferenceSample& sample, const char* quantity) {
  return "LittleCMS cross-check [" + std::string(sample.name) + "]: " +
         quantity;
}

// Several samples below sit exactly on a gamut boundary, so an inconsistency
// between the declared source's forward and inverse matrices can push them
// outside the source-gamut check and make the mapping throw rather than return
// a comparable value. Letting that escape would end the executable at the first
// such sample and silently skip every later check. Report the throw as the
// failure it is and keep going; destination-matrix errors remain covered by the
// comparison and gamut assertions.
bool try_map(const EncodedRgb& input, const GamutMapOptions& options,
             const std::string& name, camera_iq::GamutMappingResult& result) {
  try {
    result = map_encoded_rgb_to_gamut(input, options);
    return true;
  } catch (const std::runtime_error& error) {
    check(false, name + " threw: " + error.what());
    return false;
  }
}

std::string input_name(const EncodedRgb& input) {
  std::ostringstream name;
  name.precision(6);
  name << "r=" << input.r << ",g=" << input.g << ",b=" << input.b;
  return name.str();
}

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

  constexpr EncodedRgb kSrgbBlueEncodedInP3{0.0, 0.0,
                                             0.9595880266758096};
  const std::array<ReferenceSample, 5> common_gamut = {{
      {"neutral-50", {0.5, 0.5, 0.5}},
      {"cool-neutral", {0.35, 0.40, 0.45}},
      {"warm-neutral", {0.60, 0.50, 0.40}},
      {"srgb-red-encoded-in-p3",
       {0.91748755732516563, 0.20028680774084706,
        0.13856059121111408}},
      {"srgb-blue-encoded-in-p3", kSrgbBlueEncodedInP3},
  }};
  for (const auto& sample : common_gamut) {
    camera_iq::GamutMappingResult ours;
    if (!try_map(sample.input, options, check_name(sample, "mapping"), ours)) {
      continue;
    }
    check(ours.input_in_destination,
          check_name(sample, "selected color is common-gamut"));
    const auto reference = lcms_convert(sample.input, true);
    check_near(ours.output_encoded.r, reference.r, kReferenceTolerance,
               check_name(sample, "encoded red"));
    check_near(ours.output_encoded.g, reference.g, kReferenceTolerance,
               check_name(sample, "encoded green"));
    check_near(ours.output_encoded.b, reference.b, kReferenceTolerance,
               check_name(sample, "encoded blue"));
  }

  // Display-P3 and sRGB share the blue-primary chromaticity. This independent
  // point isolates the Display-P3 source blue column and should land on the
  // full-code sRGB blue primary.
  camera_iq::GamutMappingResult blue;
  if (try_map(kSrgbBlueEncodedInP3, options,
              "LittleCMS cross-check [shared-blue-primary]: mapping", blue)) {
    check_near(blue.output_encoded.r, 0.0, kReferenceTolerance,
               "LittleCMS cross-check [shared-blue-primary]: encoded red");
    check_near(blue.output_encoded.g, 0.0, kReferenceTolerance,
               "LittleCMS cross-check [shared-blue-primary]: encoded green");
    check_near(blue.output_encoded.b, 1.0, kReferenceTolerance,
               "LittleCMS cross-check [shared-blue-primary]: encoded blue");
  }

  // Saturated Display-P3 red, green, yellow, cyan, and magenta are outside
  // sRGB, so the forward arm cannot compare those full-code source corners.
  // Reversing direction adds every full-code sRGB primary and secondary while
  // remaining in the common gamut. The primaries isolate the sRGB-to-XYZ
  // source-matrix columns; the composed comparison also checks the independent
  // XYZ-to-Display-P3 inverse against LittleCMS.
  GamutMapOptions into_p3;
  into_p3.source = RgbColorSpace::Srgb;
  into_p3.destination = RgbColorSpace::DisplayP3;
  into_p3.intent = GamutMapIntent::BoundaryProjection;

  const std::array<ReferenceSample, 9> srgb_samples = {{
      {"red", {1.0, 0.0, 0.0}},
      {"green", {0.0, 1.0, 0.0}},
      {"blue", {0.0, 0.0, 1.0}},
      {"yellow", {1.0, 1.0, 0.0}},
      {"cyan", {0.0, 1.0, 1.0}},
      {"magenta", {1.0, 0.0, 1.0}},
      {"white", {1.0, 1.0, 1.0}},
      {"black", {0.0, 0.0, 0.0}},
      {"linear-segment-red", {0.02, 0.0, 0.0}},
  }};
  for (const auto& sample : srgb_samples) {
    camera_iq::GamutMappingResult ours;
    if (!try_map(sample.input, into_p3, check_name(sample, "mapping"), ours)) {
      continue;
    }
    check(ours.input_in_destination,
          check_name(sample, "sRGB sample is inside Display-P3"));
    check(!ours.modified,
          check_name(sample, "in-gamut sample is a colorimetric identity"));
    const auto reference = lcms_convert(sample.input, false);
    check_near(ours.output_encoded.r, reference.r, kReferenceTolerance,
               check_name(sample, "sRGB-to-P3 encoded red"));
    check_near(ours.output_encoded.g, reference.g, kReferenceTolerance,
               check_name(sample, "sRGB-to-P3 encoded green"));
    check_near(ours.output_encoded.b, reference.b, kReferenceTolerance,
               check_name(sample, "sRGB-to-P3 encoded blue"));
  }

  // A compact cube supplements the named edge cases so the tolerance is not
  // justified by primaries and a few hand-picked points alone. Adjacent values
  // around the sRGB transfer breakpoint exercise both curve branches.
  constexpr std::array<double, 6> kSweepLevels = {
      0.0, 0.02, 0.04045, 0.04046, 0.5, 1.0};
  double worst_error = 0.0;
  EncodedRgb worst_input{};
  const char* worst_channel = "none";
  bool every_input_in_destination = true;
  bool every_input_preserved = true;
  std::size_t successful_comparisons = 0;
  for (double r : kSweepLevels) {
    for (double g : kSweepLevels) {
      for (double b : kSweepLevels) {
        const EncodedRgb input{r, g, b};
        camera_iq::GamutMappingResult ours;
        if (!try_map(input, into_p3,
                     "LittleCMS cross-check [216-point sRGB cube; " +
                         input_name(input) + "]: mapping",
                     ours)) {
          continue;
        }
        ++successful_comparisons;
        const auto reference = lcms_convert(input, false);
        every_input_in_destination &= ours.input_in_destination;
        every_input_preserved &= !ours.modified;
        const std::array<double, 3> errors = {
            std::abs(ours.output_encoded.r - reference.r),
            std::abs(ours.output_encoded.g - reference.g),
            std::abs(ours.output_encoded.b - reference.b),
        };
        constexpr std::array<const char*, 3> kChannels = {"red", "green",
                                                           "blue"};
        for (std::size_t channel = 0; channel < errors.size(); ++channel) {
          if (errors[channel] > worst_error) {
            worst_error = errors[channel];
            worst_input = input;
            worst_channel = kChannels[channel];
          }
        }
      }
    }
  }
  constexpr std::size_t kExpectedComparisons =
      kSweepLevels.size() * kSweepLevels.size() * kSweepLevels.size();
  std::ostringstream completion;
  completion << "LittleCMS cross-check [216-point sRGB cube]: completed "
             << successful_comparisons << "/" << kExpectedComparisons
             << " comparisons";
  const bool sweep_complete = successful_comparisons == kExpectedComparisons;
  check(sweep_complete, completion.str());
  if (sweep_complete) {
    check(every_input_in_destination,
          "LittleCMS cross-check [216-point sRGB cube]: every input is inside "
          "Display-P3");
    check(every_input_preserved,
          "LittleCMS cross-check [216-point sRGB cube]: every input remains "
          "colorimetrically unchanged");
    std::ostringstream sweep_result;
    sweep_result.precision(3);
    sweep_result << "LittleCMS cross-check [216-point sRGB cube]: worst "
                 << worst_channel << " error " << std::scientific
                 << worst_error << " at (" << worst_input.r << ", "
                 << worst_input.g << ", " << worst_input.b
                 << ") is within the declared tolerance";
    check(worst_error <= kReferenceTolerance, sweep_result.str());
  }
}
