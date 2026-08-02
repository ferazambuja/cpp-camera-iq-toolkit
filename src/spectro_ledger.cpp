#include "camera_iq/spectro_ledger.hpp"

#include <set>
#include <stdexcept>
#include <string_view>

namespace camera_iq {

namespace {

constexpr std::string_view kHeader =
    "group_id,repeat_index,canonical_path,sha256,alias_paths";

// The ledger is written without quoting, so a comma inside a field would shift
// every field after it. Requiring exactly five is what turns that into a
// refusal instead of a path silently truncated at its first comma.
constexpr std::size_t kFieldCount = 5;

[[noreturn]] void refuse(const std::string& what) {
  throw std::runtime_error("spectro ledger: " + what);
}

std::vector<std::string_view> split(std::string_view text, char separator) {
  std::vector<std::string_view> out;
  std::size_t start = 0;
  for (;;) {
    const std::size_t end = text.find(separator, start);
    if (end == std::string_view::npos) {
      out.push_back(text.substr(start));
      return out;
    }
    out.push_back(text.substr(start, end - start));
    start = end + 1;
  }
}

// Source-relative is the whole safety property of the ledger: it names files
// inside an archive root the caller chooses, and must not be able to reach
// anything outside it. A carriage return fails the control-character rule, so a
// CRLF ledger is refused here rather than misreported as a bad digest.
void require_source_relative(std::string_view value, const char* field) {
  const std::string name(field);
  const std::string shown(value);
  if (value.empty()) refuse(name + " is empty");
  if (value.front() == '/') refuse(name + " is an absolute path: " + shown);
  if (value.front() == '~') refuse(name + " is home-relative: " + shown);
  if (value.find('\\') != std::string_view::npos) {
    refuse(name + " uses a backslash separator: " + shown);
  }
  if (value.find(':') != std::string_view::npos) {
    refuse(name + " contains a colon, which can introduce a scheme or drive: " +
           shown);
  }
  for (const char character : value) {
    if (static_cast<unsigned char>(character) < 32) {
      refuse(name + " contains a control character");
    }
  }
  for (const std::string_view component : split(value, '/')) {
    if (component.empty()) refuse(name + " has an empty path component: " + shown);
    if (component == "." || component == "..") {
      refuse(name + " has a relative component: " + shown);
    }
  }
}

void require_digest(std::string_view value) {
  const std::string shown(value);
  if (value.size() != 64) refuse("sha256 is not 64 characters: " + shown);
  for (const char character : value) {
    const bool is_digit = character >= '0' && character <= '9';
    const bool is_lower_hex = character >= 'a' && character <= 'f';
    if (!is_digit && !is_lower_hex) {
      refuse("sha256 is not lowercase hexadecimal: " + shown);
    }
  }
}

std::size_t parse_repeat_index(std::string_view value) {
  const std::string shown(value);
  // Nine digits cannot overflow the accumulator below, and no repeat group in
  // any plausible archive approaches it.
  if (value.empty() || value.size() > 9) {
    refuse("repeat_index is not a small positive integer: " + shown);
  }
  std::size_t parsed = 0;
  for (const char character : value) {
    if (character < '0' || character > '9') {
      refuse("repeat_index is not a number: " + shown);
    }
    parsed = parsed * 10 + static_cast<std::size_t>(character - '0');
  }
  if (parsed < 1) refuse("repeat_index is below one");
  return parsed;
}

std::vector<std::string> parse_aliases(std::string_view field) {
  std::vector<std::string> aliases;
  if (field.empty()) return aliases;
  for (const std::string_view alias : split(field, ';')) {
    require_source_relative(alias, "alias_paths");
    aliases.emplace_back(alias);
  }
  return aliases;
}

}  // namespace

std::vector<SpectroLedgerGroup> read_spectro_ledger(const std::string& csv_text) {
  std::vector<std::string_view> lines = split(csv_text, '\n');
  if (!lines.empty() && lines.back().empty()) lines.pop_back();
  if (lines.empty() || lines.front() != kHeader) {
    refuse("header is not the committed schema");
  }
  lines.erase(lines.begin());
  if (lines.empty()) refuse("ledger declares no readings");

  std::vector<SpectroLedgerGroup> groups;
  std::set<std::string> seen_groups;
  std::set<std::string> canonical_paths;
  std::set<std::string> alias_paths;
  std::set<std::string> digests;

  for (const std::string_view line : lines) {
    const std::vector<std::string_view> fields = split(line, ',');
    if (fields.size() != kFieldCount) {
      refuse("row does not have exactly five fields: " + std::string(line));
    }
    SpectroLedgerEntry entry;
    entry.group_id.assign(fields[0]);
    if (entry.group_id.empty()) refuse("group_id is empty");
    entry.repeat_index = parse_repeat_index(fields[1]);
    require_source_relative(fields[2], "canonical_path");
    entry.canonical_path.assign(fields[2]);
    require_digest(fields[3]);
    entry.sha256.assign(fields[3]);
    entry.alias_paths = parse_aliases(fields[4]);

    if (!canonical_paths.insert(entry.canonical_path).second) {
      refuse("canonical_path appears twice: " + entry.canonical_path);
    }
    // Two rows sharing a digest are one capture counted twice, which would
    // enlarge a group's sample size without adding a measurement.
    if (!digests.insert(entry.sha256).second) {
      refuse("two readings declare the same content digest: " + entry.sha256);
    }
    for (const std::string& alias : entry.alias_paths) {
      if (!alias_paths.insert(alias).second) {
        refuse("alias_paths appears twice: " + alias);
      }
    }

    // A group owns a contiguous run of rows. Resuming one later would make
    // membership depend on scanning the whole file, and a reader that stopped
    // at the run would average a subset while reporting it as the group.
    if (groups.empty() || groups.back().group_id != entry.group_id) {
      if (!seen_groups.insert(entry.group_id).second) {
        refuse("group resumes after another group: " + entry.group_id);
      }
      groups.push_back(SpectroLedgerGroup{entry.group_id, {}});
    }
    if (entry.repeat_index != groups.back().readings.size() + 1) {
      refuse("repeat_index is not the next in its group: " + entry.group_id);
    }
    groups.back().readings.push_back(std::move(entry));
  }

  for (const std::string& alias : alias_paths) {
    if (canonical_paths.count(alias) != 0) {
      refuse("a path is declared as both canonical and an alias: " + alias);
    }
  }
  return groups;
}

}  // namespace camera_iq
