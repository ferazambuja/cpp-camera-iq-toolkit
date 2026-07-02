#include "camera_iq/filename_meta.hpp"

#include "harness.hpp"

using camera_iq::parse_capture_filename;
using test::check;
using test::check_near;

void TESTS() {
  {
    const auto m = parse_capture_filename("CCSG_f9.0_1:100_ISO200_DSCF0299.RAF");
    check(m.group == "CCSG", "full name: group");
    check(m.aperture.has_value(), "full name: aperture present");
    if (m.aperture) check_near(*m.aperture, 9.0, 1e-9, "full name: aperture value");
    check(m.shutter_str == "1:100", "full name: shutter string");
    check(m.shutter_s.has_value(), "full name: shutter seconds present");
    if (m.shutter_s) check_near(*m.shutter_s, 0.01, 1e-9, "full name: shutter seconds");
    check(m.iso == 200, "full name: iso");
    check(m.frame == 299, "full name: frame");
  }
  {
    const auto m = parse_capture_filename("CCSG_f8.0_1:1000_DSCF0418.RAF");
    check(m.group == "CCSG", "no-ISO name: group");
    if (m.aperture) check_near(*m.aperture, 8.0, 1e-9, "no-ISO name: aperture");
    check(!m.iso.has_value(), "no-ISO name: iso absent");
    if (m.shutter_s) check_near(*m.shutter_s, 0.001, 1e-9, "no-ISO name: shutter");
    check(m.frame == 418, "no-ISO name: frame");
  }
  {
    const auto m =
        parse_capture_filename("Dark_Frame_f8.0_1:100_ISO200_DSCF0271.RAF");
    check(m.group == "Dark_Frame", "underscore group: Dark_Frame");
    check(m.frame == 271, "underscore group: frame");
  }
  {
    const auto m = parse_capture_filename("Non_unifform_f8.0_1:100_DSCF0426.RAF");
    check(m.group == "Non_unifform", "underscore group: Non_unifform");
  }
  {
    const auto m =
        parse_capture_filename("Validation_CC_f10.0_1:250_DSCF0493.RAF");
    check(m.group == "Validation_CC", "validation group: Validation_CC");
    if (m.aperture) check_near(*m.aperture, 10.0, 1e-9, "validation group: f10");
    if (m.shutter_s) check_near(*m.shutter_s, 0.004, 1e-9, "validation group: 1/250");
  }
  {
    const auto m = parse_capture_filename("DSCF0193.RAF");
    check(!m.group.has_value(), "plain name: no group");
    check(!m.aperture.has_value(), "plain name: no aperture");
    check(!m.shutter_s.has_value(), "plain name: no shutter");
    check(!m.iso.has_value(), "plain name: no iso");
    check(m.frame == 193, "plain name: frame");
  }
  {
    const auto m = parse_capture_filename("notes.txt");
    check(!m.group && !m.aperture && !m.shutter_s && !m.iso && !m.frame,
          "unrelated file: nothing parsed");
  }
  {
    // RawDigger CSV export sitting next to captures ("DSCF0193_RAF.csv").
    const auto m = parse_capture_filename("DSCF0193_RAF.csv");
    check(!m.frame.has_value(), "csv export: not treated as capture");
  }
  {
    // Case-insensitive extension, as some tools re-case it.
    const auto m = parse_capture_filename("DSCF0500.raf");
    check(m.frame == 500, "lowercase extension: frame parsed");
  }
}
