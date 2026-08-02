#include "camera_iq/commands.hpp"

#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "camera_iq/shading.hpp"
#include "harness.hpp"

using camera_iq::cmd_shading;
using camera_iq::ShadingChromatic;
using camera_iq::ShadingField;
using camera_iq::ShadingPedestal;
using camera_iq::write_shading_json;
using camera_iq::write_shading_comparison_json;
using test::check;

namespace {

int run_shading(const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
  return cmd_shading(static_cast<int>(argv.size()), argv.data());
}

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// A minimal accepted measurement: 2x2 grid, unit response, one green plane
// carrying a dead bin so the null path is exercised alongside the live one.
ShadingField accepted_field() {
  ShadingField f;
  f.valid = true;
  f.grid_cols = 2;
  f.grid_rows = 2;
  f.geometry.valid = true;
  f.geometry.gate = {0, 0, 4, 4};
  f.geometry.center = {0, 0, 2, 2};
  f.geometry.corners = {{{0, 0, 2, 2}, {2, 0, 2, 2},
                         {0, 2, 2, 2}, {2, 2, 2, 2}}};
  for (int p = 0; p < 4; ++p) {
    f.bin_median[p] = {1000.0, 900.0, 800.0, 700.0};
    f.relative[p] = {1.0, 0.9, 0.8, 0.7};
    f.center_block_median[p] = 1000.0;
    for (int q = 0; q < 4; ++q) {
      f.blocks.corner_median[q][p] = 700.0;
      f.blocks.corner_relative[q][p] = 0.7;
    }
  }
  f.gates.near_ceiling_ok = true;
  f.gates.screening_coverage_ok = true;
  f.gates.low_signal_ok = true;
  f.gates.negative_ok = true;
  f.gates.coverage_ok = true;
  f.gates.finite_ok = true;
  f.gates.min_bin_coverage = 1.0;
  f.gates.finite_frac_gate = {0.91, 0.92, 0.93, 0.94};
  f.gates.finite_frac_frame = {0.95, 0.96, 0.97, 0.98};
  for (int p = 0; p < 4; ++p) {
    f.gates.center_signal_frac[p] = 0.49;
    f.gates.near_ceiling_frac_gate[p] = 0.0;
    f.gates.near_ceiling_frac_frame[p] = 0.0;
    f.gates.negative_frac[p] = 0.0;
  }
  return f;
}

std::string serialize(const ShadingField& field, const ShadingChromatic& chroma,
                      const ShadingPedestal& pedestal) {
  std::ostringstream os;
  write_shading_json(os, "Images/Sphere/example.RAF", field, chroma, pedestal);
  return os.str();
}

}  // namespace

void TESTS() {
  // Argument handling, before any file I/O is attempted.
  check(run_shading({}) == 2, "shading command: raw argument required");
  check(run_shading({"a.RAF", "--grid"}) == 2,
        "shading command: --grid requires a value");
  check(run_shading({"a.RAF", "--grid", "16"}) == 2,
        "shading command: --grid needs WxH, not one number");
  check(run_shading({"a.RAF", "--grid", "0x12"}) == 2,
        "shading command: zero grid dimension rejected");
  check(run_shading({"a.RAF", "--gate-center-frac", "1.5"}) == 2,
        "shading command: gate fraction above 1.0 rejected");
  check(run_shading({"a.RAF", "--gate-center-frac", "nan"}) == 2,
        "shading command: non-finite gate fraction rejected");
  check(run_shading({"a.RAF", "--corner-block", "-4"}) == 2,
        "shading command: negative corner block rejected");
  check(run_shading({"a.RAF", "--near-ceiling-level", "0"}) == 2,
        "shading command: zero near-ceiling level rejected");
  check(run_shading({"definitely_missing.RAF", "--near-ceiling-max", "0",
                     "--corner-inset", "0"}) == 1,
        "shading command: strict zero policy and zero inset are accepted");
  check(run_shading({"a.RAF", "--unknown-option"}) == 2,
        "shading command: unknown option rejected");
  check(run_shading({"a.RAF", "--out", "a.RAF"}) == 2,
        "shading command: JSON output cannot alias the primary RAW");
  check(run_shading({"a.RAF", "--dark", "dark.RAF", "--csv-out",
                     "dark.RAF"}) == 2,
        "shading command: CSV output cannot alias a dark input");
  check(run_shading({"a.RAF", "--out", "result.json", "--csv-out",
                     "result.json"}) == 2,
        "shading command: JSON and CSV outputs must be distinct");
  check(run_shading({"../outside.RAF", "--dataset", "fixture"}) == 2,
        "shading command: dataset primary cannot traverse above root");
  check(run_shading({"inside.RAF", "--dark", "Images/../../outside.RAF",
                     "--dataset", "fixture"}) == 2,
        "shading command: dataset dark cannot traverse above root");

  // A comparison dark with nothing to compare is a user error, not a silently
  // ignored flag: it means the caller believes a comparison is happening.
  check(run_shading({"a.RAF", "--compare-dark", "d.RAF"}) == 2,
        "shading command: --compare-dark without --compare rejected");

  // Valid syntax reaches I/O and fails there, not in parsing.
  check(run_shading({"definitely_missing.RAF"}) == 1,
        "shading command: valid syntax reaches raw ingestion");

  // Serialization: an accepted measurement carries its maps and its gates.
  {
    ShadingField field = accepted_field();
    ShadingChromatic chroma;
    chroma.valid = true;
    chroma.c_rg = {1.0, 0.99, 0.98, 0.97};
    chroma.c_bg = {1.0, 1.01, 1.02, 1.03};
    chroma.c_g1g2 = {1.0, 1.0, 1.0,
                     std::numeric_limits<double>::quiet_NaN()};
    chroma.asymmetry_valid = true;
    chroma.green_asymmetry = 0.200;
    chroma.asymmetry_exceeds_policy = true;
    ShadingPedestal pedestal;  // no --dark supplied

    const std::string json = serialize(field, chroma, pedestal);

    check(contains(json, "\"accepted\":true"),
          "shading json: acceptance serialized");
    check(contains(json, "\"c_rg\""), "shading json: chromatic map serialized");
    check(contains(json, "null"),
          "shading json: undefined chromatic bin serialized as null");
    check(contains(json, "\"green_asymmetry\":0.2"),
          "shading json: quadrant asymmetry serialized");
    check(contains(json, "\"asymmetry_exceeds_policy\":true"),
          "shading json: asymmetry policy verdict serialized beside the value");
    check(contains(json, "\"pedestal_unverified\":true"),
          "shading json: a result without a dark frame is flagged, not silent");
    check(contains(json, "\"near_ceiling_fraction_gate\""),
          "shading json: gate-region near-ceiling fraction serialized");
    check(contains(json, "\"near_ceiling_fraction_frame\""),
          "shading json: whole-frame near-ceiling fraction serialized too");
    check(
        contains(json, "\"finite_fraction_gate\":[0.91,0.92,0.93,0.94]") &&
            contains(json, "\"finite_fraction_frame\":[0.95,0.96,0.97,0.98]") &&
            contains(json, "\"min_finite_coverage\":0.9") &&
            contains(json, "\"screening_coverage_ok\":true"),
        "shading json: screening coverage evidence and verdict are pinned");
    check(contains(json, "\"schema_version\":2") &&
              contains(json, "\"analysis_options\"") &&
              contains(json, "\"near_ceiling_max\":0.01") &&
              contains(json, "\"asymmetry_policy\":0.05"),
          "shading json: effective policy and schema are reproducible");
    check(contains(json,
                   "capture-system field response; no isolated component attribution"),
          "shading json: attribution remains composite regardless of A");
  }

  // Undefined quadrant asymmetry is not a measured zero or a false pass.
  {
    const ShadingField field = accepted_field();
    ShadingChromatic chroma;
    chroma.valid = true;
    chroma.complete = false;
    chroma.c_rg = chroma.c_bg = chroma.c_g1g2 = {1.0, 1.0, 1.0, 1.0};
    const std::string json = serialize(field, chroma, {});
    check(contains(json, "\"green_asymmetry\":null") &&
              contains(json, "\"asymmetry_exceeds_policy\":null"),
          "shading json: undefined A serializes as null, not zero/pass");
  }

  // Serialization: a rejected measurement keeps every diagnostic and nulls the
  // derived maps. This is the SFR command's contract, applied here.
  {
    ShadingField field = accepted_field();
    field.valid = false;
    field.gates.near_ceiling_ok = false;
    field.gates.screening_coverage_ok = false;
    field.gates.near_ceiling_frac_gate = {0.116, 0.116, 0.116, 0.116};
    field.gates.near_ceiling_frac_frame = {0.005, 0.005, 0.005, 0.005};
    field.gates.finite_frac_gate = {0.5, 0.6, 0.7, 0.8};
    field.gates.finite_frac_frame = {0.4, 0.5, 0.6, 0.7};
    for (int p = 0; p < 4; ++p) field.relative[p].clear();
    const ShadingChromatic chroma;  // never computed
    ShadingPedestal pedestal;

    const std::string json = serialize(field, chroma, pedestal);

    check(contains(json, "\"accepted\":false"),
          "shading json: rejection serialized");
    check(contains(json, "\"c_rg\":null"),
          "shading json: derived maps are null when rejected");
    check(contains(json, "0.116"),
          "shading json: rejected result keeps the fraction that rejected it");
    check(contains(json, "\"near_ceiling_ok\":false"),
          "shading json: the failing gate is identifiable");
    check(contains(json, "\"finite_fraction_gate\":[0.5,0.6,0.7,0.8]") &&
              contains(json, "\"finite_fraction_frame\":[0.4,0.5,0.6,0.7]") &&
              contains(json, "\"screening_coverage_ok\":false"),
          "shading json: rejected screening coverage evidence survives");
    check(contains(json, "\"center_block_median\""),
          "shading json: measured diagnostics survive rejection");
  }

  // A verified pedestal reports the residual it measured and the pairing check
  // it ran. "Matched" is a filename claim until the metadata agrees.
  {
    const ShadingField field = accepted_field();
    ShadingChromatic chroma;
    chroma.valid = true;
    chroma.c_rg = {1.0, 1.0, 1.0, 1.0};
    chroma.c_bg = {1.0, 1.0, 1.0, 1.0};
    chroma.c_g1g2 = {1.0, 1.0, 1.0, 1.0};
    chroma.asymmetry_valid = true;
    ShadingPedestal pedestal;
    pedestal.measured = true;
    pedestal.dark_file = "Images/Dark Frame/dark.RAF";
    pedestal.residual_dn = {0.2, 0.3, 0.25, 0.28};
    pedestal.max_abs_residual_dn = 0.3;
    pedestal.max_abs_spatial_residual_dn = 0.4;
    pedestal.compatible = true;
    pedestal.make_model_metadata_matches = true;
    pedestal.body_serials_present = true;
    pedestal.body_serials_match = true;
    pedestal.body_serials_consistent = true;
    pedestal.exposure_metadata_present = true;
    pedestal.exposure_metadata_matches = true;
    pedestal.full_finite_coverage = true;
    pedestal.spatial_checked = true;
    pedestal.within_tolerance = true;
    pedestal.verified = true;

    const std::string json = serialize(field, chroma, pedestal);

    check(contains(json, "\"pedestal_unverified\":false"),
          "shading json: a verified pedestal clears the flag");
    check(contains(json, "\"max_abs_residual_dn\":0.3"),
          "shading json: measured pedestal residual serialized");
    check(contains(json, "\"exposure_metadata_matches\":true"),
          "shading json: dark pairing check serialized, not assumed");
    check(contains(json, "\"body_serials_present\":true") &&
              contains(json, "\"body_serials_match\":true") &&
              contains(json, "\"body_serials_consistent\":true"),
          "shading json: physical body identity evidence stays explicit");
    check(contains(json, "\"max_abs_spatial_residual_dn\":0.4") &&
              contains(json, "\"full_finite_coverage\":true"),
          "shading json: spatial and finite dark evidence is serialized");
  }

  // Reading a dark is not enough to call the pedestal verified. A metadata or
  // tolerance failure keeps the explicit public warning.
  {
    const ShadingField field = accepted_field();
    const ShadingChromatic chroma;
    ShadingPedestal pedestal;
    pedestal.measured = true;
    pedestal.compatible = true;
    pedestal.dark_file = "dark.RAF";
    pedestal.max_abs_residual_dn = 2.0;
    pedestal.exposure_metadata_matches = true;
    pedestal.within_tolerance = false;
    pedestal.verified = false;
    const std::string json = serialize(field, chroma, pedestal);
    check(contains(json, "\"pedestal_unverified\":true"),
          "shading json: measured out-of-tolerance dark remains unverified");
    check(contains(json, "\"within_tolerance\":false"),
          "shading json: pedestal tolerance verdict is serialized");
  }

  // Referenced files serialize as basenames. The SFR command already does this
  // for its advisory tables; a public artifact must not carry the shape of a
  // private archive tree.
  {
    const ShadingField field = accepted_field();
    ShadingChromatic chroma;
    chroma.valid = true;
    chroma.c_rg = {1.0, 1.0, 1.0, 1.0};
    chroma.c_bg = {1.0, 1.0, 1.0, 1.0};
    chroma.c_g1g2 = {1.0, 1.0, 1.0, 1.0};
    chroma.asymmetry_valid = true;
    ShadingPedestal pedestal;
    pedestal.measured = true;
    pedestal.dark_file = "/archive-root/private/Dark Frame/dark.RAF";

    const std::string json = serialize(field, chroma, pedestal);

    check(contains(json, "\"dark_file\":\"dark.RAF\""),
          "shading json: referenced dark serialized as a basename");
    check(!contains(json, "/archive-root/"),
          "shading json: no absolute archive path reaches the output");
  }

  // The measured file itself is subject to the same rule. Without a dataset id
  // there is no relative label to use, and an absolute path would publish the
  // shape of a private archive tree.
  {
    const ShadingField field = accepted_field();
    const ShadingChromatic chroma;
    const ShadingPedestal pedestal;

    std::ostringstream os;
    write_shading_json(os, "/archive-root/private/Sphere/example.RAF", field,
                       chroma, pedestal);
    const std::string json = os.str();

    check(contains(json, "\"file\":\"example.RAF\""),
          "shading json: absolute input path reduced to a basename");
    check(!contains(json, "/archive-root/"),
          "shading json: archive tree shape never reaches the file field");
  }

  // Long-form CSV: one row per measured value, carrying the bin it came from,
  // the channel, the units, and whether the frame was accepted. A wide table
  // would force the reader to know the grid shape to interpret a column.
  {
    ShadingField field = accepted_field();
    ShadingChromatic chroma;
    chroma.valid = true;
    chroma.c_rg = {1.0, 0.99, 0.98, 0.97};
    chroma.c_bg = {1.0, 1.01, 1.02, 1.03};
    chroma.c_g1g2 = {1.0, 1.0, 1.0, 1.0};
    chroma.asymmetry_valid = true;
    chroma.green_asymmetry = 0.2;
    const ShadingPedestal pedestal;

    std::ostringstream os;
    camera_iq::write_shading_csv(os, "Images/Sphere/example.RAF",
                                 {"R", "G1", "G2", "B"}, field, chroma,
                                 pedestal);
    const std::string csv = os.str();

    check(csv.rfind("file,channel,cfa_position,metric,bin_row,bin_col,value,"
                    "units,accepted",
                    0) == 0,
          "shading csv: long-form header");
    check(contains(csv, ",G1,1,relative_response,0,1,0.9,ratio,true"),
          "shading csv: a map value carries its channel, bin and units");
    check(contains(csv, ",c_rg,1,1,"),
          "shading csv: chromatic maps are rows, not columns");
    check(contains(csv, "near_ceiling_fraction_gate"),
          "shading csv: gate diagnostics travel with the measurement");
    check(
        contains(csv, ",R,0,finite_fraction_gate,,,0.91,fraction,true") &&
            contains(csv, ",B,3,finite_fraction_frame,,,0.98,fraction,true") &&
            contains(csv, ",,-1,analysis_option_min_finite_coverage,,,0.9,"
                          "fraction,true") &&
            contains(csv, ",,-1,screening_coverage_ok,,,1,boolean,true"),
        "shading csv: screening coverage arrays, policy, and verdict are "
        "pinned");
    check(contains(csv, "green_asymmetry"),
          "shading csv: A is in the table, not only in the JSON");
    check(contains(csv, "analysis_option_near_ceiling_max") &&
              contains(csv, "analysis_option_asymmetry_policy") &&
              contains(csv, "pedestal_verified"),
          "shading csv: policy and pedestal verdicts are reproducible");

    std::ostringstream escaped;
    camera_iq::write_shading_csv(escaped, "Images/Sphere/a,\"b\".RAF",
                                 {"R", "G1", "G2", "B"}, field, chroma,
                                 pedestal);
    check(contains(escaped.str(), "\"Images/Sphere/a,\"\"b\"\".RAF\""),
          "shading csv: labels are RFC 4180 escaped");
  }

  // A rejected frame still produces a table, and that table cannot contain
  // response values — only the diagnostics that explain the rejection.
  {
    ShadingField field = accepted_field();
    field.valid = false;
    field.gates.near_ceiling_ok = false;
    for (int p = 0; p < 4; ++p) field.relative[p].clear();
    const ShadingChromatic chroma;
    const ShadingPedestal pedestal;

    std::ostringstream os;
    camera_iq::write_shading_csv(os, "Images/Sphere/rejected.RAF",
                                 {"R", "G1", "G2", "B"}, field, chroma,
                                 pedestal);
    const std::string csv = os.str();

    check(contains(csv, "near_ceiling_fraction_gate"),
          "shading csv: rejected frame keeps its diagnostics");
    check(!contains(csv, "relative_response"),
          "shading csv: rejected frame emits no response values");
    check(!contains(csv, ",c_rg,"),
          "shading csv: rejected frame emits no chromatic values");
  }

  // Comparison mode is one JSON document with both measurements and the
  // repeatability statistic. Accepting --compare and then discarding it would
  // be worse than rejecting the option because the caller would trust a check
  // that never ran.
  {
    ShadingField primary = accepted_field();
    ShadingField repeat = accepted_field();
    repeat.blocks.corner_relative[0][0] = 0.69;
    ShadingChromatic chroma;
    chroma.valid = true;
    chroma.complete = true;
    chroma.c_rg = chroma.c_bg = chroma.c_g1g2 = {1.0, 1.0, 1.0, 1.0};
    const auto comparison =
        camera_iq::compare_shading_fields(primary, repeat);
    std::ostringstream os;
    write_shading_comparison_json(
        os, "primary.RAF", primary, chroma, {}, "repeat.RAF", repeat,
        chroma, {}, comparison);
    const std::string json = os.str();
    check(contains(json, "\"primary\":{"),
          "shading comparison json: primary result is present");
    check(contains(json, "\"repeat\":{"),
          "shading comparison json: repeat result is present");
    check(contains(json, "\"comparison\":{"),
          "shading comparison json: metrics are present");
    check(contains(json, "\"max_corner_delta_pp\":1"),
          "shading comparison json: repeatability delta is serialized");

    std::ostringstream unavailable;
    write_shading_comparison_json(
        unavailable, "primary.RAF", primary, chroma, {}, "repeat.RAF",
        repeat, chroma, {}, {});
    check(contains(unavailable.str(), "\"measured\":false") &&
              contains(unavailable.str(), "\"max_corner_delta_pp\":null") &&
              contains(unavailable.str(), "\"rms_corner_delta_pp\":null"),
          "shading comparison json: unmeasured deltas cannot look perfect");
  }
}
