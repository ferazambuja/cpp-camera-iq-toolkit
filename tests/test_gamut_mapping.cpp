#include "camera_iq/gamut_mapping.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "harness.hpp"

using camera_iq::EncodedRgb;
using camera_iq::GamutMapIntent;
using camera_iq::GamutMapOptions;
using camera_iq::GamutMapBranch;
using camera_iq::GamutMappingCoordinateSpace;
using camera_iq::GamutMappingResult;
using camera_iq::Lab;
using camera_iq::LinearRgb;
using camera_iq::Oklab;
using camera_iq::RgbColorSpace;
using camera_iq::Xyz;
using camera_iq::d65_white_xyz;
using camera_iq::decode_rgb;
using camera_iq::encode_rgb;
using camera_iq::find_gamut_boundary;
using camera_iq::gamut_boundary_chroma;
using camera_iq::is_in_unit_gamut;
using camera_iq::lab_to_xyz;
using camera_iq::linear_rgb_to_xyz;
using camera_iq::map_encoded_rgb_to_gamut;
using camera_iq::map_d65_lab_to_gamut;
using camera_iq::oklab_to_xyz_d65;
using camera_iq::xyz_to_lab;
using camera_iq::xyz_to_linear_rgb;
using test::check;
using test::check_near;

namespace {

double chroma(const Lab& lab) { return std::hypot(lab.a, lab.b); }

double hue_radians(const Lab& lab) { return std::atan2(lab.b, lab.a); }

void check_finite(const EncodedRgb& rgb, const char* message) {
  check(std::isfinite(rgb.r) && std::isfinite(rgb.g) && std::isfinite(rgb.b),
        message);
}

}  // namespace

void TESTS() {
  const auto half_linear = decode_rgb({0.5, 0.5, 0.5});
  check_near(half_linear.r, 0.21404114048223255, 1e-15,
             "transfer: decode 0.5");
  check_near(encode_rgb(half_linear).r, 0.5, 1e-15,
             "transfer: round trip 0.5");
  check_near(decode_rgb({0.04045, 0.0, 0.0}).r, 0.04045 / 12.92, 1e-15,
             "transfer: decode threshold");
  check_near(encode_rgb({0.0031308, 0.0, 0.0}).r, 12.92 * 0.0031308,
             1e-15, "transfer: encode threshold");

  struct PrimaryReference {
    RgbColorSpace space;
    LinearRgb rgb;
    std::array<double, 3> xyz;
    const char* name;
  };
  const std::array<PrimaryReference, 6> primaries = {{
      {RgbColorSpace::Srgb, {1, 0, 0},
       {0.41239079926595951, 0.21263900587151036,
        0.019330818715591849},
       "matrix: sRGB red"},
      {RgbColorSpace::Srgb, {0, 1, 0},
       {0.35758433938387796, 0.71516867876775593,
        0.11919477979462599},
       "matrix: sRGB green"},
      {RgbColorSpace::Srgb, {0, 0, 1},
       {0.18048078840183429, 0.072192315360733715,
        0.95053215224966058},
       "matrix: sRGB blue"},
      {RgbColorSpace::DisplayP3, {1, 0, 0},
       {0.48657094864821615, 0.22897456406974881, 0.0},
       "matrix: P3 red"},
      {RgbColorSpace::DisplayP3, {0, 1, 0},
       {0.26566769316909306, 0.69173852183650642,
        0.045113381858902638},
       "matrix: P3 green"},
      {RgbColorSpace::DisplayP3, {0, 0, 1},
       {0.1982172852343625, 0.079286914093745001,
        1.043944368900976},
       "matrix: P3 blue"},
  }};
  for (const auto& ref : primaries) {
    const auto xyz = linear_rgb_to_xyz(ref.rgb, ref.space);
    check_near(xyz.x, ref.xyz[0], 1e-15, ref.name);
    check_near(xyz.y, ref.xyz[1], 1e-15, ref.name);
    check_near(xyz.z, ref.xyz[2], 1e-15, ref.name);
    const auto round_trip = xyz_to_linear_rgb(xyz, ref.space);
    check_near(round_trip.r, ref.rgb.r, 2e-15, ref.name);
    check_near(round_trip.g, ref.rgb.g, 2e-15, ref.name);
    check_near(round_trip.b, ref.rgb.b, 2e-15, ref.name);
  }

  const auto d65 = d65_white_xyz();
  check_near(d65.x, 0.9504559270516717, 1e-15, "D65: normalized X");
  check_near(d65.y, 1.0, 1e-15, "D65: normalized Y");
  check_near(d65.z, 1.0890577507598784, 1e-15, "D65: normalized Z");

  GamutMapOptions projection;
  projection.source = RgbColorSpace::DisplayP3;
  projection.destination = RgbColorSpace::Srgb;
  projection.intent = GamutMapIntent::BoundaryProjection;

  const auto p3_red = map_encoded_rgb_to_gamut({1, 0, 0}, projection);
  check(!p3_red.input_in_destination,
        "projection: P3 red begins outside sRGB");
  check(is_in_unit_gamut(p3_red.destination_linear_after, 1e-12),
        "projection: P3 red ends inside sRGB");
  check(p3_red.output_chroma < p3_red.input_chroma,
        "projection: P3 red chroma is reduced");
  check_near(p3_red.output_lab.l, p3_red.input_lab.l, 1e-12,
             "projection: lightness preserved");
  check_near(hue_radians(p3_red.output_lab), hue_radians(p3_red.input_lab),
             1e-12, "projection: hue preserved");
  check_finite(p3_red.output_encoded, "projection: finite encoded output");
  check(p3_red.modified, "projection: out-of-gamut P3 red is modified");
  check(p3_red.branch == GamutMapBranch::FixedLhRadialBoundaryClip,
        "projection: radial branch is explicit");

  const auto srgb_red_from_p3 = map_encoded_rgb_to_gamut(
      {0.91748755732516563, 0.20028680774084706,
       0.13856059121111408},
      projection);
  check(srgb_red_from_p3.input_in_destination,
        "identity: sRGB red expressed in P3 is in destination");
  check(!srgb_red_from_p3.modified,
        "identity: common-gamut color is not modified");
  check_near(srgb_red_from_p3.output_encoded.r, 1.0, 2e-9,
             "identity: colorimetric identity produces sRGB red R");
  check_near(srgb_red_from_p3.output_encoded.g, 0.0, 2e-9,
             "identity: colorimetric identity produces sRGB red G");
  check_near(srgb_red_from_p3.output_encoded.b, 0.0, 2e-9,
             "identity: colorimetric identity produces sRGB red B");

  const auto gray = map_encoded_rgb_to_gamut({0.5, 0.5, 0.5}, projection);
  check(gray.input_in_destination, "projection: neutral is in sRGB");
  check_near(gray.output_encoded.r, 0.5, 1e-12,
             "projection: in-gamut neutral R unchanged");
  check_near(gray.output_encoded.g, 0.5, 1e-12,
             "projection: in-gamut neutral G unchanged");
  check_near(gray.output_encoded.b, 0.5, 1e-12,
             "projection: in-gamut neutral B unchanged");
  check(!gray.boundary_evidence_applicable,
        "projection: neutral has no CIELAB radial-boundary evidence");

  GamutMapOptions soft = projection;
  soft.intent = GamutMapIntent::SoftChromaCompression;
  soft.knee_fraction = 0.75;
  const auto soft_gray = map_encoded_rgb_to_gamut({0.5, 0.5, 0.5}, soft);
  check(!soft_gray.boundary_evidence_applicable,
        "soft compression: neutral has no CIELAB radial-boundary evidence");
  check(!soft_gray.modified,
        "soft compression: neutral stays an identity result");
  const Lab p3_red_lab = xyz_to_lab(
      linear_rgb_to_xyz({1, 0, 0}, RgbColorSpace::DisplayP3), d65);
  const double hue = std::atan2(p3_red_lab.b, p3_red_lab.a);
  const double source_boundary = gamut_boundary_chroma(
      p3_red_lab.l, hue, RgbColorSpace::DisplayP3);
  const double destination_boundary = gamut_boundary_chroma(
      p3_red_lab.l, hue, RgbColorSpace::Srgb);
  check(source_boundary > destination_boundary,
        "boundary: P3 extends beyond sRGB at red hue");
  check_near(destination_boundary, 93.86561347147861, 2e-9,
             "boundary: P3-red ray first sRGB exit reference");
  const auto boundary =
      find_gamut_boundary(p3_red_lab.l, hue, RgbColorSpace::Srgb);
  check(boundary.converged, "boundary: search reports convergence");
  check(boundary.lower_chroma == boundary.chroma,
        "boundary: returned endpoint is the conservative lower bracket");
  check(boundary.upper_chroma > boundary.lower_chroma,
        "boundary: final bracket is ordered");
  check(boundary.bracket_width <= 1e-10,
        "boundary: final bracket records refinement precision");
  const Lab just_inside{p3_red_lab.l,
                        (boundary.chroma - 1e-7) * std::cos(hue),
                        (boundary.chroma - 1e-7) * std::sin(hue)};
  const Lab just_outside{p3_red_lab.l,
                         (boundary.upper_chroma + 1e-7) * std::cos(hue),
                         (boundary.upper_chroma + 1e-7) * std::sin(hue)};
  check(is_in_unit_gamut(xyz_to_linear_rgb(
                             lab_to_xyz(just_inside, d65),
                             RgbColorSpace::Srgb),
                         1e-12),
        "boundary: lower side is in gamut");
  check(!is_in_unit_gamut(xyz_to_linear_rgb(
                              lab_to_xyz(just_outside, d65),
                              RgbColorSpace::Srgb),
                          1e-12),
        "boundary: upper side is out of gamut");

  const double knee = soft.knee_fraction * destination_boundary;
  const Lab below_knee{p3_red_lab.l, (knee - 1e-5) * std::cos(hue),
                       (knee - 1e-5) * std::sin(hue)};
  const auto below = map_d65_lab_to_gamut(below_knee, soft);
  check_near(below.output_chroma, knee - 1e-5, 1e-10,
             "soft: identity below knee");
  check(below.branch == GamutMapBranch::ProtectedCoreIdentity,
        "soft: protected-core identity branch is explicit");

  const Lab above_knee{p3_red_lab.l, (knee + 1e-5) * std::cos(hue),
                       (knee + 1e-5) * std::sin(hue)};
  const auto above = map_d65_lab_to_gamut(above_knee, soft);
  check(std::abs(above.output_chroma - below.output_chroma) < 3e-5,
        "soft: continuous at knee");
  check((above.output_chroma - knee) / 1e-5 > 0.999,
        "soft: compressor has unit slope at the knee");

  const double shoulder_chroma =
      knee + 0.5 * (destination_boundary - knee);
  const Lab shoulder{p3_red_lab.l, shoulder_chroma * std::cos(hue),
                     shoulder_chroma * std::sin(hue)};
  const auto shoulder_mapped = map_d65_lab_to_gamut(shoulder, soft);
  check(shoulder_mapped.input_in_destination,
        "soft: test shoulder begins inside destination");
  check(shoulder_mapped.modified,
        "soft: protected-core intent flags modified in-gamut shoulder");
  check(shoulder_mapped.branch == GamutMapBranch::SoftChromaCompression,
        "soft: compression branch is explicit");
  check(shoulder_mapped.output_chroma < shoulder_chroma,
        "soft: in-gamut shoulder is deliberately compressed");

  double previous = knee;
  for (int i = 0; i <= 12; ++i) {
    const double t = static_cast<double>(i) / 12.0;
    const double input_chroma = knee + t * (source_boundary - knee);
    const Lab sample{p3_red_lab.l, input_chroma * std::cos(hue),
                     input_chroma * std::sin(hue)};
    const auto mapped = map_d65_lab_to_gamut(sample, soft);
    check(mapped.output_chroma + 1e-10 >= previous,
          "soft: mapped chroma is monotone");
    check(mapped.output_chroma <= destination_boundary + 1e-9,
          "soft: mapped chroma does not exceed destination boundary");
    check(is_in_unit_gamut(mapped.destination_linear_after, 1e-9),
          "soft: mapped sample is inside destination");
    check_near(mapped.output_lab.l, sample.l, 1e-12,
               "soft: lightness preserved");
    check_near(hue_radians(mapped.output_lab), hue, 1e-12,
               "soft: hue preserved");
    previous = mapped.output_chroma;
  }

  const std::array<EncodedRgb, 8> cube = {{{0, 0, 0}, {0, 0, 1},
                                           {0, 1, 0}, {0, 1, 1},
                                           {1, 0, 0}, {1, 0, 1},
                                           {1, 1, 0}, {1, 1, 1}}};
  for (const auto& corner : cube) {
    const auto mapped = map_encoded_rgb_to_gamut(corner, soft);
    check_finite(mapped.output_encoded, "corners: finite encoded output");
    check(is_in_unit_gamut(mapped.destination_linear_after, 1e-9),
          "corners: mapped output is in destination");
  }

  const auto p3_yellow_xyz =
      linear_rgb_to_xyz({1, 1, 0}, RgbColorSpace::DisplayP3);
  const Lab p3_yellow_lab = xyz_to_lab(p3_yellow_xyz, d65);
  const double p3_yellow_hue = hue_radians(p3_yellow_lab);
  const double p3_yellow_connected_boundary = gamut_boundary_chroma(
      p3_yellow_lab.l, p3_yellow_hue, RgbColorSpace::DisplayP3);
  check(chroma(p3_yellow_lab) > p3_yellow_connected_boundary + 50.0,
        "boundary: P3 yellow demonstrates a disconnected radial re-entry");
  check(is_in_unit_gamut(xyz_to_linear_rgb(p3_yellow_xyz,
                                           RgbColorSpace::DisplayP3),
                         1e-12),
        "boundary: P3 yellow itself remains a legal source color");
  const auto p3_yellow_mapped =
      map_encoded_rgb_to_gamut({1, 1, 0}, soft);
  check(is_in_unit_gamut(p3_yellow_mapped.destination_linear_after, 1e-9),
        "boundary: disconnected source ray still maps into destination");

  const auto radial_boundary_contract =
      map_encoded_rgb_to_gamut({0, 0.25, 0.5}, projection);
  check(radial_boundary_contract.modified,
        "radial evidence: fixture reaches the destination boundary");
  check(radial_boundary_contract.output_mapping_chroma ==
            radial_boundary_contract.destination_boundary_chroma,
        "radial evidence: reported mapped chroma is the exact solved boundary");

  GamutMapOptions oklch_radial = projection;
  oklch_radial.intent = GamutMapIntent::OklchBoundaryProjection;
  const auto neutral_oklch =
      map_encoded_rgb_to_gamut({0.25, 0.25, 0.25}, oklch_radial);
  check(!neutral_oklch.input_oklch.hue_defined,
        "OkLCh radial: neutral input has no mapping direction");
  check(!neutral_oklch.boundary_evidence_applicable,
        "OkLCh radial: neutral input does not publish an arbitrary boundary");
  check(!neutral_oklch.modified && neutral_oklch.output_in_destination,
        "OkLCh radial: neutral input remains an accepted identity result");
  const auto p3_yellow_oklch =
      map_encoded_rgb_to_gamut({1, 1, 0}, oklch_radial);
  check(p3_yellow_oklch.mapping_coordinate_space ==
            GamutMappingCoordinateSpace::OklabD65,
        "OkLCh radial: mapping coordinate space is explicit");
  check(p3_yellow_oklch.branch ==
            GamutMapBranch::FixedOklchRadialBoundaryClip,
        "OkLCh radial: branch is explicit");
  check(p3_yellow_oklch.output_in_destination,
        "OkLCh radial: P3 yellow ends in sRGB");
  check_near(p3_yellow_oklch.output_oklch.l,
             p3_yellow_oklch.input_oklch.l, 1e-12,
             "OkLCh radial: lightness is preserved");
  check_near(p3_yellow_oklch.output_oklch.h_degrees,
             p3_yellow_oklch.input_oklch.h_degrees, 1e-10,
             "OkLCh radial: hue coordinate is preserved");
  check(p3_yellow_oklch.output_mapping_chroma > 0.20,
        "OkLCh radial: P3 yellow retains useful chroma");
  check(p3_yellow_oklch.output_mapping_chroma <
            p3_yellow_oklch.input_mapping_chroma,
        "OkLCh radial: P3 yellow chroma is reduced");

  GamutMapOptions css_local_minde = projection;
  css_local_minde.intent = GamutMapIntent::CssColor4LocalMinde;
  const auto p3_yellow_css =
      map_encoded_rgb_to_gamut({1, 1, 0}, css_local_minde);
  check(p3_yellow_css.mapping_coordinate_space ==
            GamutMappingCoordinateSpace::OklabD65,
        "CSS Local MINDE: mapping coordinate space is explicit");
  check(p3_yellow_css.local_minde.applicable,
        "CSS Local MINDE: typed algorithm evidence is present");
  check_near(p3_yellow_css.local_minde.jnd, 0.02, 0.0,
             "CSS Local MINDE: dated CSS JND is pinned");
  check_near(p3_yellow_css.local_minde.epsilon, 0.0001, 0.0,
             "CSS Local MINDE: dated CSS epsilon is pinned");
  check(p3_yellow_css.local_minde.iterations > 0,
        "CSS Local MINDE: P3 yellow exercises binary search");
  check(p3_yellow_css.output_in_destination,
        "CSS Local MINDE: P3 yellow ends in sRGB");
  check(p3_yellow_css.local_minde.final_delta_e_ok < 0.02,
        "CSS Local MINDE: final local clip is below one JND");
  check(p3_yellow_css.output_mapping_chroma >=
            p3_yellow_oklch.output_mapping_chroma,
        "CSS Local MINDE: local clip does not discard more yellow chroma than radial clipping");
  // Independent ColorAide 5.1 `oklch-chroma` oracle. The CSS binary search
  // terminates on a chroma tolerance, so component agreement is bounded by
  // the dated algorithm's epsilon rather than asserted bit-exact.
  check_near(p3_yellow_css.output_encoded.r, 0.9962332729609733, 5e-4,
             "CSS Local MINDE: ColorAide 5.1 P3-yellow R oracle");
  check_near(p3_yellow_css.output_encoded.g, 0.9990138958496102, 5e-4,
             "CSS Local MINDE: ColorAide 5.1 P3-yellow G oracle");
  check_near(p3_yellow_css.output_encoded.b, 0.0, 5e-4,
             "CSS Local MINDE: ColorAide 5.1 P3-yellow B oracle");

  const auto common_gray_css =
      map_encoded_rgb_to_gamut({0.5, 0.5, 0.5}, css_local_minde);
  check(!common_gray_css.modified,
        "CSS Local MINDE: in-gamut colors retain relative-colorimetric identity");
  check(common_gray_css.branch == GamutMapBranch::IdentityNoMappingRequired,
        "CSS Local MINDE: in-gamut identity branch is explicit");

  GamutMapOptions same_space = soft;
  same_space.source = RgbColorSpace::Srgb;
  same_space.destination = RgbColorSpace::Srgb;
  const auto same_space_red =
      map_encoded_rgb_to_gamut({1, 0, 0}, same_space);
  check(!same_space_red.modified,
        "identity: equal source and destination preserve color");
  check(same_space_red.branch == GamutMapBranch::IdentityNoGamutContraction,
        "identity: no-contraction branch is distinct from protected core");

  GamutMapOptions wider_destination = soft;
  wider_destination.source = RgbColorSpace::Srgb;
  wider_destination.destination = RgbColorSpace::DisplayP3;
  const auto srgb_into_p3 =
      map_encoded_rgb_to_gamut({1, 0, 0}, wider_destination);
  check(!srgb_into_p3.modified,
        "identity: a wider destination does not activate compression");
  check(srgb_into_p3.branch == GamutMapBranch::IdentityNoGamutContraction,
        "identity: boundary-bracket comparison identifies no contraction");

  const double wrapped_boundary = gamut_boundary_chroma(
      p3_red_lab.l, hue + 2.0 * 3.14159265358979323846,
      RgbColorSpace::Srgb);
  check_near(wrapped_boundary, destination_boundary, 1e-10,
             "boundary: hue wrap is deterministic");

  const auto near_black =
      map_encoded_rgb_to_gamut({1e-6, 2e-6, 3e-6}, soft);
  check_finite(near_black.output_encoded,
               "adversarial: near-black output finite");
  check(near_black.output_in_destination,
        "adversarial: near-black output in destination");
  const auto near_white = map_encoded_rgb_to_gamut(
      {0.999999, 0.999998, 0.999997}, soft);
  check_finite(near_white.output_encoded,
               "adversarial: near-white output finite");
  check(near_white.output_in_destination,
        "adversarial: near-white output in destination");
  const Oklab powerless_oklab{0.5, 0.000003, 0.000002};
  const Xyz powerless_xyz = oklab_to_xyz_d65(powerless_oklab);
  GamutMapOptions oklch_identity;
  oklch_identity.source = RgbColorSpace::DisplayP3;
  oklch_identity.destination = RgbColorSpace::Srgb;
  oklch_identity.intent = GamutMapIntent::OklchBoundaryProjection;
  const auto powerless_identity = map_d65_lab_to_gamut(
      xyz_to_lab(powerless_xyz, d65_white_xyz()), oklch_identity);
  check(powerless_identity.input_in_destination,
        "domain: powerless-hue fixture is in the destination gamut");
  check(!powerless_identity.modified,
        "domain: OkLCh radial preserves powerless-hue in-gamut identity");
  check_near(powerless_identity.output_oklab.a,
             powerless_identity.input_oklab.a, 1e-14,
             "domain: powerless-hue identity preserves OkLab a");
  check_near(powerless_identity.output_oklab.b,
             powerless_identity.input_oklab.b, 1e-14,
             "domain: powerless-hue identity preserves OkLab b");
  const auto repeated = map_encoded_rgb_to_gamut({1, 0, 0}, soft);
  const auto repeated_again = map_encoded_rgb_to_gamut({1, 0, 0}, soft);
  check(repeated.output_encoded.r == repeated_again.output_encoded.r &&
            repeated.output_encoded.g == repeated_again.output_encoded.g &&
            repeated.output_encoded.b == repeated_again.output_encoded.b,
        "determinism: repeated mapping is bit-identical");

  GamutMapOptions invalid_knee = soft;
  invalid_knee.knee_fraction = 1.0;
  bool knee_threw = false;
  try {
    (void)map_encoded_rgb_to_gamut({1, 0, 0}, invalid_knee);
  } catch (const std::runtime_error&) {
    knee_threw = true;
  }
  check(knee_threw, "soft: knee fraction 1 is rejected as discontinuous");

  GamutMapOptions near_unity_knee = soft;
  near_unity_knee.knee_fraction = std::nextafter(1.0, 0.0);
  const auto near_unity_mapped =
      map_encoded_rgb_to_gamut({1, 0, 0}, near_unity_knee);
  check(near_unity_mapped.output_chroma >
            0.99 * near_unity_mapped.destination_boundary_chroma,
        "soft: representable near-unity knee does not collapse chroma");
  check(near_unity_mapped.output_in_destination,
        "soft: representable near-unity knee remains in destination");

  camera_iq::GamutBoundaryOptions exhausted_options;
  exhausted_options.refinement_iterations = 1;
  exhausted_options.chroma_tolerance = 1e-12;
  const auto exhausted = find_gamut_boundary(
      p3_red_lab.l, hue, RgbColorSpace::Srgb, exhausted_options);
  check(!exhausted.converged,
        "boundary: exhausted refinement is not reported as converged");
  bool exhausted_threw = false;
  try {
    (void)gamut_boundary_chroma(p3_red_lab.l, hue, RgbColorSpace::Srgb,
                                exhausted_options);
  } catch (const std::runtime_error&) {
    exhausted_threw = true;
  }
  check(exhausted_threw, "boundary: unconverged convenience search rejected");

  // This legal Display-P3 ray leaves sRGB for only about 0.10 C* and then
  // re-enters. A 0.25-C* membership scan samples both sides as in-gamut and
  // incorrectly selects a much later boundary.
  const double narrow_exit_lightness = 96.23855934179699;
  const double narrow_exit_hue = 1.8012425907027048;
  const double narrow_exit_chroma = 57.69;
  const Lab narrow_exit{
      narrow_exit_lightness, narrow_exit_chroma * std::cos(narrow_exit_hue),
      narrow_exit_chroma * std::sin(narrow_exit_hue)};
  check(is_in_unit_gamut(xyz_to_linear_rgb(lab_to_xyz(narrow_exit, d65),
                                           RgbColorSpace::DisplayP3),
                         1e-12),
        "boundary roots: adversarial color is legal Display-P3");
  check(!is_in_unit_gamut(xyz_to_linear_rgb(lab_to_xyz(narrow_exit, d65),
                                            RgbColorSpace::Srgb),
                          1e-12),
        "boundary roots: adversarial color lies in narrow sRGB exit");
  const auto narrow_boundary = find_gamut_boundary(
      narrow_exit_lightness, narrow_exit_hue, RgbColorSpace::Srgb);
  check(narrow_boundary.chroma > 57.5 &&
            narrow_boundary.chroma < narrow_exit_chroma,
        "boundary roots: first narrow exit is found before re-entry");
  const auto narrow_mapped = map_d65_lab_to_gamut(narrow_exit, projection);
  check(narrow_mapped.output_in_destination,
        "boundary roots: narrow-exit color maps successfully");

  camera_iq::GamutBoundaryOptions oversized_rgb_tolerance;
  oversized_rgb_tolerance.gamut_tolerance = 0.5;
  bool lightness_threw = false;
  try {
    (void)find_gamut_boundary(-0.25, 0.0, RgbColorSpace::Srgb,
                              oversized_rgb_tolerance);
  } catch (const std::runtime_error&) {
    lightness_threw = true;
  }
  check(lightness_threw,
        "domain: RGB gamut tolerance does not relax the L* range");

  bool oklch_lightness_threw = false;
  try {
    (void)map_d65_lab_to_gamut({-0.25, 0.0, 0.0}, oklch_radial);
  } catch (const std::runtime_error&) {
    oklch_lightness_threw = true;
  }
  check(oklch_lightness_threw,
        "domain: OkLCh path enforces the declared Lab L* range");

  GamutMapOptions oklch_endpoint = oklch_radial;
  oklch_endpoint.oklch_boundary.gamut_tolerance = 0.5;
  bool oklch_endpoint_threw = false;
  GamutMappingResult oklch_endpoint_result;
  try {
    oklch_endpoint_result =
        map_d65_lab_to_gamut({-5e-11, 0.0, 0.0}, oklch_endpoint);
  } catch (const std::runtime_error&) {
    oklch_endpoint_threw = true;
  }
  check(!oklch_endpoint_threw,
        "domain: OkLCh path honors the public Lab endpoint tolerance");
  check_near(oklch_endpoint_result.input_lab.l, 0.0, 1e-12,
             "domain: OkLCh path clamps tolerated Lab endpoint roundoff");

  GamutMapOptions zero_tolerance = projection;
  zero_tolerance.boundary.gamut_tolerance = 0.0;
  bool zero_tolerance_threw = false;
  try {
    (void)map_encoded_rgb_to_gamut({1, 0, 0}, zero_tolerance);
  } catch (const std::runtime_error&) {
    zero_tolerance_threw = true;
  }
  check(zero_tolerance_threw,
        "domain: zero gamut tolerance is rejected as numerically undefined");

  const auto barely_outside_source_xyz =
      linear_rgb_to_xyz({-5e-11, 0.5, 0.5}, RgbColorSpace::Srgb);
  const Lab barely_outside_source_lab =
      xyz_to_lab(barely_outside_source_xyz, d65);
  bool source_tolerance_threw = false;
  try {
    (void)map_d65_lab_to_gamut(barely_outside_source_lab, same_space);
  } catch (const std::runtime_error&) {
    source_tolerance_threw = true;
  }
  check(source_tolerance_threw,
        "domain: source admission uses the configured gamut tolerance");

  bool nan_threw = false;
  try {
    (void)map_encoded_rgb_to_gamut(
        {std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0}, projection);
  } catch (const std::runtime_error&) {
    nan_threw = true;
  }
  check(nan_threw, "domain: NaN encoded input rejected");

  bool infinity_threw = false;
  try {
    (void)map_d65_lab_to_gamut(
        {50.0, std::numeric_limits<double>::infinity(), 0.0}, projection);
  } catch (const std::runtime_error&) {
    infinity_threw = true;
  }
  check(infinity_threw, "domain: infinite Lab input rejected");

  bool invalid_threw = false;
  try {
    (void)map_encoded_rgb_to_gamut({1.01, 0.0, 0.0}, projection);
  } catch (const std::runtime_error&) {
    invalid_threw = true;
  }
  check(invalid_threw, "domain: encoded source outside [0,1] rejected");

  // Retain the adversarial coverage used to challenge the mapper so it can be
  // rerun in every build. The set combines transfer-function boundaries, a
  // fixed-seed cube sample, and near-neutral colors where hue is ill-
  // conditioned. It contains exactly 3,229 encoded Display-P3 inputs.
  bool stress_no_throw = true;
  bool stress_finite = true;
  bool stress_in_gamut = true;
  bool stress_chroma_nonincreasing = true;
  bool stress_lightness_preserved = true;
  bool stress_radial_identity = true;
  bool stress_oklch_radial_contract = true;
  bool stress_css_identity = true;
  bool stress_hue_preserved = true;
  double maximum_hue_shift = 0.0;
  std::size_t stress_count = 0;
  const auto check_stress_sample = [&](const EncodedRgb& sample) {
    ++stress_count;
    try {
      const auto radial = map_encoded_rgb_to_gamut(sample, projection);
      const auto compressed = map_encoded_rgb_to_gamut(sample, soft);
      const auto ok_radial =
          map_encoded_rgb_to_gamut(sample, oklch_radial);
      const auto css = map_encoded_rgb_to_gamut(sample, css_local_minde);
      const std::array<GamutMappingResult, 4> results = {
          radial, compressed, ok_radial, css};
      for (const auto& mapped : results) {
        stress_finite =
            stress_finite && std::isfinite(mapped.output_encoded.r) &&
            std::isfinite(mapped.output_encoded.g) &&
            std::isfinite(mapped.output_encoded.b);
        stress_in_gamut =
            stress_in_gamut && mapped.output_in_destination &&
            is_in_unit_gamut(mapped.destination_linear_after, 1e-12);
        if (mapped.mapping_coordinate_space ==
            GamutMappingCoordinateSpace::CielabD65) {
          stress_chroma_nonincreasing =
              stress_chroma_nonincreasing &&
              mapped.output_chroma <= mapped.input_chroma + 1e-10;
          stress_lightness_preserved =
              stress_lightness_preserved &&
              std::abs(mapped.output_lab.l - mapped.input_lab.l) <= 1e-12;
        }
        if (mapped.mapping_coordinate_space ==
                GamutMappingCoordinateSpace::CielabD65 &&
            mapped.input_chroma > 1e-6 && mapped.output_chroma > 1e-6) {
          const double hue_delta = std::abs(std::atan2(
              std::sin(hue_radians(mapped.output_lab) -
                       hue_radians(mapped.input_lab)),
              std::cos(hue_radians(mapped.output_lab) -
                       hue_radians(mapped.input_lab))));
          maximum_hue_shift = std::max(maximum_hue_shift, hue_delta);
          stress_hue_preserved = stress_hue_preserved && hue_delta <= 2e-7;
        }
      }
      if (radial.input_in_destination) {
        stress_radial_identity =
            stress_radial_identity && !radial.modified &&
            std::abs(radial.output_lab.l - radial.input_lab.l) <= 1e-12 &&
            std::abs(radial.output_lab.a - radial.input_lab.a) <= 1e-12 &&
            std::abs(radial.output_lab.b - radial.input_lab.b) <= 1e-12;
      }
      stress_oklch_radial_contract =
          stress_oklch_radial_contract &&
          ok_radial.output_mapping_chroma <=
              ok_radial.input_mapping_chroma + 1e-12 &&
          std::abs(ok_radial.output_oklch.l - ok_radial.input_oklch.l) <=
              1e-12 &&
          (!ok_radial.input_oklch.hue_defined ||
           std::abs(ok_radial.output_oklch.h_degrees -
                    ok_radial.input_oklch.h_degrees) <= 1e-8);
      if (css.input_in_destination) {
        stress_css_identity = stress_css_identity && !css.modified;
      }
      if (ok_radial.input_in_destination) {
        stress_oklch_radial_contract =
            stress_oklch_radial_contract && !ok_radial.modified;
      }
    } catch (const std::runtime_error&) {
      stress_no_throw = false;
    }
  };

  const std::array<double, 9> boundary_values = {
      0.0,
      1e-12,
      std::nextafter(0.04045, 0.0),
      0.04045,
      std::nextafter(0.04045, 1.0),
      0.25,
      0.5,
      std::nextafter(1.0, 0.0),
      1.0,
  };
  for (double r : boundary_values) {
    for (double g : boundary_values) {
      for (double b : boundary_values) check_stress_sample({r, g, b});
    }
  }

  std::uint64_t state = 0xd1b54a32d192ed03ULL;
  const auto next_unit = [&]() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<double>(state >> 11) * 0x1.0p-53;
  };
  for (int i = 0; i < 2000; ++i) {
    check_stress_sample({next_unit(), next_unit(), next_unit()});
  }
  for (int i = 0; i < 500; ++i) {
    const double center = (static_cast<double>(i) + 0.5) / 500.0;
    const double offset = static_cast<double>((i % 5) - 2) * 1e-10;
    check_stress_sample({std::clamp(center + offset, 0.0, 1.0), center,
                         std::clamp(center - offset, 0.0, 1.0)});
  }

  check(stress_count == 3229, "stress: deterministic sample count is pinned");
  check(stress_no_throw, "stress: every legal encoded input maps");
  check(stress_finite, "stress: every mapped output is finite");
  check(stress_in_gamut,
        "stress: every mapped output is independently in destination");
  check(stress_chroma_nonincreasing,
        "stress: mapped chroma never increases");
  check(stress_lightness_preserved,
        "stress: fixed-Lstar contract holds");
  check(stress_radial_identity,
        "stress: radial intent preserves destination-in-gamut inputs");
  check(stress_oklch_radial_contract,
        "stress: OkLCh radial intent preserves L/h and never increases mapping chroma");
  check(stress_css_identity,
        "stress: CSS relative-colorimetric intent preserves in-gamut inputs");
  check(stress_hue_preserved && maximum_hue_shift <= 2e-7,
        "stress: Lab hue is preserved away from the neutral singularity");
}
