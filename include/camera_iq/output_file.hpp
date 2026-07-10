#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string_view>

namespace camera_iq {

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
