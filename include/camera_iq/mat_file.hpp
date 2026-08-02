#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace camera_iq {

// One numeric array read from a MATLAB v5 MAT-file. `values` is in MATLAB's
// column-major order, unchanged, so a caller that knows the shape can index it
// without this reader guessing an interpretation.
struct MatArray {
  std::vector<std::size_t> dims;
  std::vector<double> values;
};

// A MATLAB struct with numeric fields, keyed by field name.
using MatStruct = std::map<std::string, MatArray>;

// Reads the first struct variable from the bytes of a MATLAB v5 MAT-file.
//
// These files store their whole payload as one compressed element, so this
// inflates before parsing. Malformed input throws std::runtime_error naming
// what failed: a reader that returned an empty struct instead would be
// indistinguishable from a file that genuinely holds no fields.
MatStruct read_mat_struct(const std::string& bytes);

}  // namespace camera_iq
