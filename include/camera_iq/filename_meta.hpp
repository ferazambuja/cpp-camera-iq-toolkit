#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace camera_iq {

// Exposure metadata parsed from a capture filename.
//
// The CLRS-589 capture campaign encodes exposure in filenames:
//   <Group>_f<aperture>_1:<denominator>[_ISO<iso>]_DSCF<frame>.RAF
// e.g. "CCSG_f9.0_1:100_ISO200_DSCF0299.RAF", "Dark_Frame_f8.0_1:10_DSCF0439.RAF"
// Plain camera names ("DSCF0193.RAF") carry only the frame index.
//
// All fields are optional: absent means "not encoded in the filename".
// Filename metadata is a cross-check against EXIF, never a replacement.
struct FilenameMeta {
  std::optional<std::string> group;        // "CCSG", "Dark_Frame", "Validation_CC"
  std::optional<double> aperture;          // f-number, e.g. 9.0
  std::optional<double> shutter_s;         // shutter time in seconds
  std::optional<std::string> shutter_str;  // as written, e.g. "1:100"
  std::optional<int> iso;                  // ISO value if encoded
  std::optional<int> frame;                // DSCF frame index, e.g. 299
};

// Parses a bare filename (no directory components). Never throws;
// unrecognized names return a FilenameMeta with all fields empty.
FilenameMeta parse_capture_filename(std::string_view filename);

}  // namespace camera_iq
