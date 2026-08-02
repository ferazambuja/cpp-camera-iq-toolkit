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
// This is a subset reader for the shape the spectroradiometer archive stores,
// not a general MAT implementation. It accepts little-endian v5 files, walks
// compressed and uncompressed element streams, and returns one struct of
// numeric arrays. It does not implement cell arrays, objects, char arrays,
// sparse arrays, complex parts, or v7.3/HDF5 files, and it flattens a struct
// array to the field set of its first element rather than preserving the array.
//
// Malformed input throws std::runtime_error naming what failed, including a
// numeric payload that is not a whole number of samples and dimensions that
// disagree with the value count. A reader that returned an empty struct instead
// would be indistinguishable from a file that genuinely holds no fields.
MatStruct read_mat_struct(const std::string& bytes);

}  // namespace camera_iq
