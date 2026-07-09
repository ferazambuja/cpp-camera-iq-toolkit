#pragma once

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace camera_iq {

inline std::string trim_csv_cell(std::string_view cell) {
  const auto first = cell.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos) return {};
  const auto last = cell.find_last_not_of(" \t\r\n");
  return std::string(cell.substr(first, last - first + 1));
}

inline std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> out;
  std::string cell;
  std::istringstream in(line);
  while (std::getline(in, cell, ',')) {
    out.push_back(trim_csv_cell(cell));
  }
  if (!line.empty() && line.back() == ',') out.emplace_back();
  return out;
}

inline std::optional<double> parse_double(std::string_view text) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(std::string(text), &consumed);
    if (consumed != text.size() || !std::isfinite(value)) return std::nullopt;
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

inline std::optional<int> parse_int(std::string_view text) {
  const auto value = parse_double(text);
  if (!value) return std::nullopt;
  const double rounded = std::round(*value);
  if (std::abs(*value - rounded) > 1e-9) return std::nullopt;
  // Guard the float-to-int cast: out-of-range conversion is UB and silently
  // platform-divergent (arm64 saturates, x86-64 wraps).
  if (rounded < static_cast<double>(std::numeric_limits<int>::min()) ||
      rounded > static_cast<double>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  return static_cast<int>(rounded);
}

}  // namespace camera_iq
