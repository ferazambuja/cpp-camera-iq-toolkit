#include "camera_iq/output_file.hpp"

#include <fstream>
#include <iostream>
#include <system_error>

namespace camera_iq {
namespace {

void remove_partial_file(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

void report_failure(std::ostream& err, std::string_view command_name,
                    std::string_view action,
                    const std::filesystem::path& path) {
  err << "camera_iq " << command_name << ": " << action << " " << path
      << "\n";
}

}  // namespace

bool finish_output_stream_checked(std::ostream& os,
                                  const std::filesystem::path& path,
                                  std::string_view command_name,
                                  std::ostream& err,
                                  bool remove_partial,
                                  bool append_newline) {
  if (append_newline) {
    os << "\n";
  }
  os.flush();
  if (!os) {
    report_failure(err, command_name, "failed writing", path);
    if (remove_partial) {
      remove_partial_file(path);
    }
    return false;
  }
  return true;
}

bool write_output_file_checked(
    const std::filesystem::path& path,
    std::string_view command_name,
    const std::function<void(std::ostream&)>& write_body,
    std::ostream& err,
    bool append_newline) {
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      report_failure(err, command_name, "cannot create parent directory for",
                     path);
      return false;
    }
  }

  std::ofstream os(path, std::ios::binary);
  if (!os) {
    report_failure(err, command_name, "cannot write", path);
    return false;
  }

  try {
    write_body(os);
  } catch (...) {
    remove_partial_file(path);
    throw;
  }

  if (!finish_output_stream_checked(os, path, command_name, err,
                                    /*remove_partial=*/true,
                                    append_newline)) {
    return false;
  }

  os.close();
  if (!os) {
    report_failure(err, command_name, "failed closing", path);
    remove_partial_file(path);
    return false;
  }
  return true;
}

}  // namespace camera_iq
