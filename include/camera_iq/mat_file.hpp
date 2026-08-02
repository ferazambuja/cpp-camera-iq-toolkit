#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace camera_iq {

// One numeric array read from a MATLAB v5 MAT-file. `values` is in MATLAB's
// column-major order, unchanged, so a caller that knows the shape can index it
// without this reader guessing an interpretation.
struct MatArray {
  std::vector<std::size_t> dims;
  std::vector<double> values;
  // MATLAB logical arrays use a numeric storage element but retain Boolean
  // semantics. Values are validated as 0/1 before widening to double.
  bool logical = false;
};

// A MATLAB struct with numeric fields, keyed by field name.
using MatStruct = std::map<std::string, MatArray>;

// Reads one named scalar struct variable from MATLAB v5 MAT-file bytes.
//
// This is a subset reader for the shape the spectroradiometer archive stores,
// not a general MAT implementation. It accepts little-endian v5 files, walks
// compressed and uncompressed element streams, skips unrelated top-level
// payloads after inspecting their common matrix header, and returns one struct
// of numeric arrays. It does not implement cell arrays, objects, char arrays,
// sparse arrays, complex arrays, nested structs, non-scalar structs, or
// v7.3/HDF5 files. Logical uint8 arrays are preserved explicitly. Duplicate
// requested variables or field names are errors.
//
// Malformed input throws std::runtime_error naming what failed, including a
// numeric payload that is not a whole number of samples, invalid or overflowing
// dimensions, unsupported array classes or storage types, malformed padding,
// and excessive nesting.
// A reader that returned an empty struct instead would be indistinguishable from
// a file that genuinely holds no fields.
//   variable_name       name of the struct variable to return. Files may hold
//                       more than one, and taking whichever comes first makes
//                       the result depend on write order.
//   max_inflated_bytes  cap on total inflated bytes across the file, including
//                       sibling and nested compressed elements. A small file can
//                       declare an unbounded expansion, and refusing is
//                       diagnosable where an out-of-memory abort is not. The
//                       archive's largest payload is a few kilobytes.
MatStruct read_mat_struct(const std::string& bytes,
                          std::string_view variable_name = "measurements",
                          std::size_t max_inflated_bytes = 64u << 20);

}  // namespace camera_iq
