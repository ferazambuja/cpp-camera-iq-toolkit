#include "camera_iq/json_writer.hpp"

#include <sstream>

#include "harness.hpp"

using camera_iq::json_escape;
using camera_iq::JsonWriter;
using test::check;

void TESTS() {
  check(json_escape("plain") == "plain", "escape: plain passthrough");
  check(json_escape("say \"hi\"") == "say \\\"hi\\\"", "escape: quotes");
  check(json_escape("a\\b") == "a\\\\b", "escape: backslash");
  check(json_escape("line1\nline2") == "line1\\nline2", "escape: newline");
  check(json_escape("tab\there") == "tab\\there", "escape: tab");
  check(json_escape(std::string_view("\x01", 1)) == "\\u0001",
        "escape: control char");
  check(json_escape("caf\xC3\xA9") == "caf\xC3\xA9", "escape: utf-8 passthrough");

  {
    std::ostringstream os;
    JsonWriter w(os);
    w.begin_object();
    w.end_object();
    check(os.str() == "{}", "empty object");
  }
  {
    std::ostringstream os;
    JsonWriter w(os);
    w.begin_array();
    w.end_array();
    check(os.str() == "[]", "empty array");
  }
  {
    std::ostringstream os;
    JsonWriter w(os);
    w.begin_object();
    w.key("a");
    w.value(1);
    w.key("b");
    w.value("x");
    w.key("c");
    w.begin_array();
    w.value(true);
    w.null();
    w.value(1.5);
    w.end_array();
    w.end_object();
    check(os.str() == R"({"a":1,"b":"x","c":[true,null,1.5]})",
          "object with mixed values");
  }
  {
    std::ostringstream os;
    JsonWriter w(os);
    w.begin_array();
    w.begin_object();
    w.key("n");
    w.value(0.001);
    w.end_object();
    w.begin_object();
    w.key("f");
    w.value(9.0);
    w.end_object();
    w.end_array();
    check(os.str() == R"([{"n":0.001},{"f":9}])", "array of objects, numbers");
  }
  {
    std::ostringstream os;
    JsonWriter w(os);
    w.begin_object();
    w.key("path");
    w.value("Images/Dark Frame/x.RAF");
    w.end_object();
    check(os.str() == R"({"path":"Images/Dark Frame/x.RAF"})",
          "path with spaces survives");
  }
}
