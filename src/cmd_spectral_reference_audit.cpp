#include "camera_iq/commands.hpp"

#include "camera_iq/color_reference.hpp"
#include "camera_iq/colorimetry.hpp"
#include "camera_iq/json_writer.hpp"
#include "camera_iq/output_file.hpp"
#include "camera_iq/spectral_reference_audit.hpp"
#include "camera_iq/spectro_colorimetry.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace camera_iq {
namespace {

void usage(std::ostream& output) {
  output <<
      "Usage: camera_iq spectral-reference-audit [options]\n\n"
      "Audit retained CGATS interchange, embedded colorimetry, and a paired\n"
      "reflectance series. The four CGATS inputs must preserve one measurement\n"
      "by stable SAMPLE_ID. Illuminant and observer files are always explicit.\n\n"
      "Required file options:\n"
      "  --spectrashop FILE\n"
      "  --alternate-spectrashop FILE\n"
      "  --babelcolor FILE\n"
      "  --layout-export FILE\n"
      "  --repeat-first FILE\n"
      "  --repeat-second FILE\n"
      "  --d65 FILE\n"
      "  --observer-10 FILE\n"
      "  --observer-2 FILE\n"
      "  --d55 FILE\n"
      "  --out-json FILE\n"
      "  --out-csv FILE\n"
      "  -h, --help\n";
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path.generic_string());
  }
  std::ostringstream result;
  result << input.rdbuf();
  if (!input && !input.eof()) {
    throw std::runtime_error("cannot read " + path.generic_string());
  }
  return result.str();
}

void write_colorimetry(JsonWriter& writer,
                       const SpectralReferenceColorimetryAudit& audit) {
  writer.begin_object();
  writer.key("illuminant");
  writer.value(audit.illuminant);
  writer.key("observer");
  writer.value(audit.observer);
  writer.key("integration");
  writer.value(audit.integration);
  writer.key("observer_metadata_conflict");
  writer.value(audit.observer_metadata_conflict);
  writer.key("lab_patch_count");
  writer.value(static_cast<std::int64_t>(audit.lab_patch_count));
  writer.key("mean_delta_e_76");
  writer.value(audit.mean_delta_e_76);
  writer.key("max_delta_e_76");
  writer.value(audit.max_delta_e_76);
  writer.key("xyz_patch_count");
  writer.value(static_cast<std::int64_t>(audit.xyz_patch_count));
  writer.key("mean_xyz_relative_l2");
  writer.value(audit.mean_xyz_relative_l2);
  writer.key("max_xyz_relative_l2");
  writer.value(audit.max_xyz_relative_l2);
  writer.end_object();
}

void write_schema(JsonWriter& writer, std::string_view export_id,
                  const SpectralReference& reference) {
  writer.begin_object();
  writer.key("export_id");
  writer.value(export_id);
  writer.key("declared_field_count");
  if (reference.cgats_schema &&
      reference.cgats_schema->declared_field_count) {
    writer.value(static_cast<std::int64_t>(
        *reference.cgats_schema->declared_field_count));
  } else {
    writer.null();
  }
  writer.key("actual_field_count");
  writer.value(static_cast<std::int64_t>(
      reference.cgats_schema ? reference.cgats_schema->actual_field_count : 0));
  writer.key("field_count_matches");
  writer.value(reference.cgats_schema &&
               reference.cgats_schema->field_count_matches);
  writer.key("declared_set_count");
  if (reference.cgats_schema && reference.cgats_schema->declared_set_count) {
    writer.value(static_cast<std::int64_t>(
        *reference.cgats_schema->declared_set_count));
  } else {
    writer.null();
  }
  writer.key("actual_set_count");
  writer.value(static_cast<std::int64_t>(
      reference.cgats_schema ? reference.cgats_schema->actual_set_count : 0));
  writer.key("set_count_matches");
  writer.value(reference.cgats_schema &&
               reference.cgats_schema->set_count_matches);
  writer.end_object();
}

void write_json(std::ostream& output, const SpectralReference& spectrashop,
                const SpectralReference& alternate_spectrashop,
                const SpectralReference& babelcolor,
                const SpectralReference& layout_export,
                const SpectralReferenceInterchangeComparison& alternate,
                const SpectralReferenceInterchangeComparison& babel,
                const SpectralReferenceInterchangeComparison& layout,
                const SpectralReferenceColorimetryAudit& matching,
                const SpectralReferenceColorimetryAudit& observer_alternative,
                const SpectralReferenceColorimetryAudit& xyz_oracle,
                const SpectralReferenceRepeatAudit& repeat) {
  JsonWriter writer(output);
  writer.begin_object();
  writer.key("schema_version");
  writer.value(1);
  writer.key("cgats_evidence_scope");
  writer.value("one_measurement_reserialized");
  writer.key("observer_selection");
  writer.value("explicit_caller_supplied_never_inferred_from_conflicting_metadata");
  writer.key("cgats_exports");
  writer.begin_array();
  write_schema(writer, "spectrashop_primary", spectrashop);
  write_schema(writer, "spectrashop_alternate_layout", alternate_spectrashop);
  write_schema(writer, "babelcolor_xyz", babelcolor);
  write_schema(writer, "babelcolor_layout", layout_export);
  writer.end_array();
  writer.key("source_observer_declarations");
  writer.begin_array();
  if (spectrashop.cgats_schema) {
    for (const auto& declaration :
         spectrashop.cgats_schema->observer_declarations) {
      writer.value(declaration);
    }
  }
  writer.end_array();
  writer.key("all_four_exports_exact_spectral_content");
  writer.value(alternate.exact_spectral_multiset_identity &&
               babel.exact_spectral_multiset_identity &&
               layout.exact_spectral_multiset_identity);
  writer.key("stable_id_pairing_across_exports");
  writer.value(alternate.spectra_match_by_stable_id &&
               babel.spectra_match_by_stable_id &&
               layout.spectra_match_by_stable_id);
  writer.key("layout_labels_differ");
  writer.value(!alternate.layout_labels_match_by_stable_id ||
               !babel.layout_labels_match_by_stable_id ||
               !layout.layout_labels_match_by_stable_id);
  writer.key("spectrashop_d65_10_degree");
  write_colorimetry(writer, matching);
  writer.key("spectrashop_d65_2_degree_alternative");
  write_colorimetry(writer, observer_alternative);
  writer.key("babelcolor_d65_2_degree_xyz");
  write_colorimetry(writer, xyz_oracle);
  writer.key("candidate_repeat");
  writer.begin_object();
  writer.key("evidence_scope");
  writer.value(repeat.evidence_scope);
  writer.key("illuminant");
  writer.value(repeat.illuminant);
  writer.key("observer");
  writer.value(repeat.observer);
  writer.key("patch_count");
  writer.value(static_cast<std::int64_t>(repeat.patch_count));
  writer.key("mean_reflectance_rms");
  writer.value(repeat.mean_reflectance_rms);
  writer.key("max_reflectance_rms");
  writer.value(repeat.max_reflectance_rms);
  writer.key("mean_delta_e_76");
  writer.value(repeat.mean_delta_e_76);
  writer.key("max_delta_e_76");
  writer.value(repeat.max_delta_e_76);
  writer.end_object();
  writer.end_object();
}

void write_csv(std::ostream& output,
               const SpectralReferenceRepeatAudit& repeat) {
  // The retained source tables carry far less than twelve decimal places.
  // Fixed precision keeps the public aggregate byte-stable across build modes
  // without discarding measurement-relevant information.
  output << std::fixed << std::setprecision(12);
  output << "patch_id,reflectance_rms,delta_e_76\n";
  for (const auto& patch : repeat.patches) {
    const bool quote = patch.id.find_first_of(",\"\r\n") != std::string::npos;
    if (quote) output << '"';
    for (const char character : patch.id) {
      if (character == '"') output << '"';
      output << character;
    }
    if (quote) output << '"';
    output << ',' << patch.reflectance_rms << ','
           << patch.delta_e_76 << '\n';
  }
}

}  // namespace

int cmd_spectral_reference_audit(int argc, char** argv) {
  if (argc == 1 && (std::string_view(argv[0]) == "--help" ||
                    std::string_view(argv[0]) == "-h")) {
    usage(std::cout);
    return 0;
  }
  std::filesystem::path spectrashop_path;
  std::filesystem::path alternate_spectrashop_path;
  std::filesystem::path babelcolor_path;
  std::filesystem::path layout_path;
  std::filesystem::path repeat_first_path;
  std::filesystem::path repeat_second_path;
  std::filesystem::path d65_path;
  std::filesystem::path observer10_path;
  std::filesystem::path observer2_path;
  std::filesystem::path d55_path;
  std::filesystem::path out_json;
  std::filesystem::path out_csv;
  const auto assign = [&](std::string_view option,
                          const std::filesystem::path& value) {
    if (option == "--spectrashop") spectrashop_path = value;
    else if (option == "--alternate-spectrashop") alternate_spectrashop_path = value;
    else if (option == "--babelcolor") babelcolor_path = value;
    else if (option == "--layout-export") layout_path = value;
    else if (option == "--repeat-first") repeat_first_path = value;
    else if (option == "--repeat-second") repeat_second_path = value;
    else if (option == "--d65") d65_path = value;
    else if (option == "--observer-10") observer10_path = value;
    else if (option == "--observer-2") observer2_path = value;
    else if (option == "--d55") d55_path = value;
    else if (option == "--out-json") out_json = value;
    else if (option == "--out-csv") out_csv = value;
    else return false;
    return true;
  };
  for (int index = 0; index < argc; ++index) {
    const std::string_view option = argv[index];
    if (option == "--help" || option == "-h") {
      usage(std::cout);
      return 0;
    }
    if (index + 1 >= argc) {
      std::cerr << "camera_iq spectral-reference-audit: " << option
                << " requires a value\n";
      return 2;
    }
    if (!assign(option, argv[++index])) {
      std::cerr << "camera_iq spectral-reference-audit: unknown option '"
                << option << "'\n";
      return 2;
    }
  }
  const std::vector<std::filesystem::path> inputs = {
      spectrashop_path, alternate_spectrashop_path, babelcolor_path,
      layout_path, repeat_first_path, repeat_second_path, d65_path,
      observer10_path, observer2_path, d55_path};
  if (out_json.empty() || out_csv.empty() ||
      std::any_of(inputs.begin(), inputs.end(),
                  [](const auto& path) { return path.empty(); })) {
    std::cerr << "camera_iq spectral-reference-audit: every documented file "
                 "option is required\n";
    return 2;
  }
  for (const auto& input : inputs) {
    if (output_path_aliases_input(out_json, input) ||
        output_path_aliases_input(out_csv, input)) {
      std::cerr << "camera_iq spectral-reference-audit: input and output paths "
                   "must differ\n";
      return 2;
    }
  }
  if (output_path_aliases_input(out_json, out_csv)) {
    std::cerr << "camera_iq spectral-reference-audit: JSON and CSV outputs "
                 "must differ\n";
    return 2;
  }

  try {
    const auto spectrashop = read_spectral_reference_cgats(
        spectrashop_path, "spectrashop_primary");
    const auto alternate_spectrashop = read_spectral_reference_cgats(
        alternate_spectrashop_path, "spectrashop_alternate_layout");
    const auto babelcolor = read_spectral_reference_cgats(
        babelcolor_path, "babelcolor_xyz");
    const auto layout =
        read_spectral_reference_cgats(layout_path, "babelcolor_layout");
    const auto repeat_first = read_spectral_reference_csv(
        repeat_first_path, "candidate_repeat_first");
    const auto repeat_second = read_spectral_reference_csv(
        repeat_second_path, "candidate_repeat_second");
    const auto observer10 = read_spectro_cmf_csv(read_text(observer10_path));
    const auto observer2 = read_spectro_cmf_csv(read_text(observer2_path));
    const auto d65 = read_spectrum_csv_interpolated(
        d65_path, spectrashop.wavelengths_nm);
    const auto d55 = read_spectrum_csv_interpolated(
        d55_path, repeat_first.wavelengths_nm);
    const auto matching = audit_spectral_reference_colorimetry(
        spectrashop, d65, observer10, "D65", "CIE_1964_10_degree");
    const auto alternative = audit_spectral_reference_colorimetry(
        spectrashop, d65, observer2, "D65", "CIE_1931_2_degree");
    const auto xyz_oracle = audit_spectral_reference_colorimetry(
        babelcolor, d65, observer2, "D65", "CIE_1931_2_degree");
    const auto repeat = audit_spectral_reference_repeat(
        repeat_first, repeat_second, d55, observer2, "D55",
        "CIE_1931_2_degree");
    const auto alternate_interchange =
        compare_spectral_reference_interchange(spectrashop,
                                               alternate_spectrashop);
    const auto babel_interchange =
        compare_spectral_reference_interchange(spectrashop, babelcolor);
    const auto layout_interchange =
        compare_spectral_reference_interchange(babelcolor, layout);

    const auto proves_one_reserialized_measurement = [](const auto& item) {
      return item.stable_id_set_matches && item.spectra_match_by_stable_id &&
             item.exact_spectral_multiset_identity;
    };
    if (!proves_one_reserialized_measurement(alternate_interchange) ||
        !proves_one_reserialized_measurement(babel_interchange) ||
        !proves_one_reserialized_measurement(layout_interchange)) {
      throw std::runtime_error(
          "CGATS exports do not preserve one measurement by stable identity");
    }

    if (!write_output_file_checked(
            out_json, "spectral-reference-audit",
            [&](std::ostream& output) {
              write_json(output, spectrashop, alternate_spectrashop,
                         babelcolor, layout, alternate_interchange,
                         babel_interchange, layout_interchange, matching,
                         alternative, xyz_oracle, repeat);
            },
            std::cerr) ||
        !write_output_file_checked(
            out_csv, "spectral-reference-audit",
            [&](std::ostream& output) { write_csv(output, repeat); },
            std::cerr, false)) {
      return 1;
    }
    std::cerr << "spectral-reference-audit: checked four CGATS exports and "
              << repeat.patch_count << " candidate repeat patches\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "camera_iq spectral-reference-audit: " << error.what()
              << '\n';
    return 1;
  }
}

}  // namespace camera_iq
