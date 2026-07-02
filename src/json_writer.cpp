#include "camera_iq/json_writer.hpp"

#include <charconv>
#include <cstdio>
#include <string>

namespace camera_iq {

JsonWriter::JsonWriter(std::ostream& os) : os_(os) {}

void JsonWriter::separator() {
  if (after_key_) {
    // Value directly follows its key; the key already wrote ':'.
    after_key_ = false;
    return;
  }
  if (!has_element_.empty()) {
    if (has_element_.back()) os_ << ',';
    has_element_.back() = true;
  }
}

void JsonWriter::begin_object() {
  separator();
  os_ << '{';
  has_element_.push_back(false);
}

void JsonWriter::end_object() {
  has_element_.pop_back();
  os_ << '}';
}

void JsonWriter::begin_array() {
  separator();
  os_ << '[';
  has_element_.push_back(false);
}

void JsonWriter::end_array() {
  has_element_.pop_back();
  os_ << ']';
}

void JsonWriter::key(std::string_view k) {
  separator();
  os_ << '"' << json_escape(k) << "\":";
  after_key_ = true;
}

void JsonWriter::value(std::string_view v) {
  separator();
  os_ << '"' << json_escape(v) << '"';
}

void JsonWriter::value(const char* v) { value(std::string_view(v)); }

void JsonWriter::value(double v) {
  separator();
  // Shortest round-trip representation ("9", "0.001", "1.5").
  char buf[32];
  const auto res = std::to_chars(buf, buf + sizeof(buf), v);
  os_.write(buf, res.ptr - buf);
}

void JsonWriter::value(std::int64_t v) {
  separator();
  os_ << v;
}

void JsonWriter::value(int v) { value(static_cast<std::int64_t>(v)); }

void JsonWriter::value(bool v) {
  separator();
  os_ << (v ? "true" : "false");
}

void JsonWriter::null() {
  separator();
  os_ << "null";
}

std::string json_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  return out;
}

}  // namespace camera_iq
