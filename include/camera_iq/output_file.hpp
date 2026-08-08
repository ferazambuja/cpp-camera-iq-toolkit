#pragma once

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace camera_iq {

// Compares existing identities when possible, then normalized filesystem paths.
// Call before opening an output so a normal file cannot truncate its own input.
bool output_path_aliases_input(const std::filesystem::path& output,
                               const std::filesystem::path& input);

// Rejects an output that would truncate any of its own inputs, reporting the
// offending input by path. Empty and absent entries are skipped so a caller can
// pass optional arguments directly. Commands read their inputs and build the
// report in memory before writing, so without this check an output aliasing an
// input destroys measured data and still exits 0.
bool reject_output_aliasing_inputs(
    const std::filesystem::path& output,
    std::initializer_list<std::optional<std::filesystem::path>> inputs,
    std::string_view command_name,
    std::ostream& err);

bool finish_output_stream_checked(std::ostream& os,
                                  const std::filesystem::path& path,
                                  std::string_view command_name,
                                  std::ostream& err,
                                  bool remove_partial,
                                  bool append_newline = true);

bool write_output_file_checked(
    const std::filesystem::path& path,
    std::string_view command_name,
    const std::function<void(std::ostream&)>& write_body,
    std::ostream& err,
    bool append_newline = true);

}  // namespace camera_iq
