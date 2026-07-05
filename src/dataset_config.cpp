#include "camera_iq/dataset_config.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace camera_iq {
namespace {

class JsonCursor {
 public:
  explicit JsonCursor(std::string_view text) : text_(text) {}

  bool consume(char c) {
    skip_ws();
    if (pos_ >= text_.size() || text_[pos_] != c) return false;
    ++pos_;
    return true;
  }

  void expect(char c) {
    if (!consume(c)) {
      throw std::runtime_error("dataset config: expected JSON delimiter");
    }
  }

  std::string string() {
    skip_ws();
    if (pos_ >= text_.size() || text_[pos_] != '"') {
      throw std::runtime_error("dataset config: expected JSON string");
    }
    ++pos_;
    std::string out;
    while (pos_ < text_.size()) {
      const char c = text_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (pos_ >= text_.size()) {
        throw std::runtime_error("dataset config: bad JSON escape");
      }
      const char e = text_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default:
          throw std::runtime_error("dataset config: unsupported JSON escape");
      }
    }
    throw std::runtime_error("dataset config: unterminated JSON string");
  }

  void skip_value() {
    skip_ws();
    if (pos_ >= text_.size()) return;
    if (text_[pos_] == '{') {
      skip_object();
    } else if (text_[pos_] == '[') {
      skip_array();
    } else if (text_[pos_] == '"') {
      (void)string();
    } else {
      while (pos_ < text_.size() && text_[pos_] != ',' && text_[pos_] != '}' &&
             text_[pos_] != ']') {
        ++pos_;
      }
    }
  }

 private:
  void skip_ws() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c != ' ' && c != '\n' && c != '\r' && c != '\t') return;
      ++pos_;
    }
  }

  void skip_object() {
    expect('{');
    if (consume('}')) return;
    do {
      (void)string();
      expect(':');
      skip_value();
    } while (consume(','));
    expect('}');
  }

  void skip_array() {
    expect('[');
    if (consume(']')) return;
    do {
      skip_value();
    } while (consume(','));
    expect(']');
  }

  std::string_view text_;
  std::size_t pos_ = 0;
};

ColorReferenceSpec parse_color_reference_object(JsonCursor& cur) {
  ColorReferenceSpec spec;
  cur.expect('{');
  if (cur.consume('}')) return spec;
  do {
    const std::string key = cur.string();
    cur.expect(':');
    if (key == "id") {
      spec.id = cur.string();
    } else if (key == "role") {
      spec.role = cur.string();
    } else if (key == "format") {
      spec.format = cur.string();
    } else if (key == "path") {
      spec.path = cur.string();
    } else if (key == "source_xlsx") {
      spec.source_xlsx = cur.string();
    } else if (key == "source_sheet") {
      spec.source_sheet = cur.string();
    } else if (key == "selection_basis") {
      spec.selection_basis = cur.string();
    } else {
      cur.skip_value();
    }
  } while (cur.consume(','));
  cur.expect('}');
  return spec;
}

DatasetSpec parse_dataset_object(std::string_view id, JsonCursor& cur) {
  DatasetSpec spec;
  spec.id = std::string(id);
  cur.expect('{');
  if (cur.consume('}')) return spec;
  do {
    const std::string key = cur.string();
    cur.expect(':');
    if (key == "root") {
      spec.root = cur.string();
    } else if (key == "description") {
      spec.description = cur.string();
    } else if (key == "color_reference") {
      spec.color_reference = parse_color_reference_object(cur);
    } else {
      cur.skip_value();
    }
  } while (cur.consume(','));
  cur.expect('}');
  return spec;
}

std::string read_all(const std::filesystem::path& path) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("dataset config: cannot open " + path.string());
  }
  return std::string(std::istreambuf_iterator<char>(is),
                     std::istreambuf_iterator<char>());
}

}  // namespace

std::filesystem::path default_dataset_config_path() {
  if (const char* env = std::getenv("CAMERA_IQ_DATASETS_CONFIG")) {
    if (*env != '\0') return env;
  }
  return "configs/datasets.local.json";
}

std::map<std::string, DatasetSpec> read_dataset_config(
    const std::filesystem::path& config_path) {
  const std::string text = read_all(config_path);
  JsonCursor cur(text);
  std::map<std::string, DatasetSpec> out;

  cur.expect('{');
  if (cur.consume('}')) return out;
  do {
    const std::string top_key = cur.string();
    cur.expect(':');
    if (top_key != "datasets") {
      cur.skip_value();
      continue;
    }

    cur.expect('{');
    if (cur.consume('}')) continue;
    do {
      const std::string id = cur.string();
      cur.expect(':');
      DatasetSpec spec = parse_dataset_object(id, cur);
      if (!spec.root.empty()) out.emplace(id, std::move(spec));
    } while (cur.consume(','));
    cur.expect('}');
  } while (cur.consume(','));
  cur.expect('}');

  return out;
}

std::optional<ResolvedDataset> resolve_dataset_root(
    std::string_view root_or_id, const std::filesystem::path& config_path) {
  const std::filesystem::path direct{std::string(root_or_id)};
  if (std::filesystem::is_directory(direct)) {
    return ResolvedDataset{"", direct, direct.string(), false};
  }

  if (!std::filesystem::exists(config_path)) return std::nullopt;
  const auto datasets = read_dataset_config(config_path);
  const auto it = datasets.find(std::string(root_or_id));
  if (it == datasets.end()) return std::nullopt;
  return ResolvedDataset{it->second.id, it->second.root,
                         dataset_root_label(it->second.id), true};
}

std::string dataset_root_label(std::string_view dataset_id) {
  return "dataset:" + std::string(dataset_id);
}

std::string dataset_file_label(std::string_view dataset_id,
                               const std::filesystem::path& relative_path) {
  std::string label = dataset_root_label(dataset_id);
  const std::string rel = relative_path.generic_string();
  if (!rel.empty()) {
    label += '/';
    label += rel;
  }
  return label;
}

}  // namespace camera_iq
