#pragma once

#include <cstdint>
#include <ostream>
#include <string_view>
#include <vector>

namespace camera_iq {

// Minimal streaming JSON writer (compact output, no whitespace).
// Hand-rolled on purpose: manifest output is flat and predictable, and the
// toolkit stays dependency-free apart from LibRaw. Commas and nesting are
// managed internally; misuse (e.g. value without key inside an object) is
// not diagnosed — callers are the toolkit's own serialization code with tests.
class JsonWriter {
 public:
  explicit JsonWriter(std::ostream& os);

  void begin_object();
  void end_object();
  void begin_array();
  void end_array();

  // Object key; must be followed by a value or container.
  void key(std::string_view k);

  void value(std::string_view v);
  void value(const char* v);
  void value(double v);
  void value(std::int64_t v);
  void value(int v);
  void value(bool v);
  void null();

 private:
  void separator();

  std::ostream& os_;
  // One flag per open container: true once the first element was written.
  std::vector<bool> has_element_;
  bool after_key_ = false;
};

// Escapes a string for embedding in a JSON document (no surrounding quotes).
std::string json_escape(std::string_view s);

}  // namespace camera_iq
