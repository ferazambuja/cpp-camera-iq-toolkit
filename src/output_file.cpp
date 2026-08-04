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

bool output_path_uses_symlink(const std::filesystem::path& path,
                              std::error_code& error) {
  error.clear();
  const std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (error) {
    return false;
  }

  std::filesystem::path current;
  for (const auto& component : absolute.lexically_normal()) {
    current /= component;
    const auto status = std::filesystem::symlink_status(current, error);
    if (error) {
      if (error == std::errc::no_such_file_or_directory) {
        error.clear();
        return false;
      }
      return false;
    }
    // macOS exposes stable system roots such as /var and /tmp as top-level
    // aliases. They precede the caller-controlled portion of the path; reject
    // symlinks below that boundary, where an output can be redirected.
    if (std::filesystem::is_symlink(status) &&
        current.parent_path() != current.root_path()) {
      return true;
    }
  }
  return false;
}

std::filesystem::path comparable_path(const std::filesystem::path& path) {
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) return canonical;
  const auto absolute = std::filesystem::absolute(path, error);
  return error ? path.lexically_normal() : absolute.lexically_normal();
}

}  // namespace

bool output_path_aliases_input(const std::filesystem::path& output,
                               const std::filesystem::path& input) {
  if (output.empty() || input.empty()) return false;
  std::error_code error;
  if (std::filesystem::equivalent(output, input, error) && !error) return true;
  return comparable_path(output) == comparable_path(input);
}

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
  std::error_code link_error;
  if (output_path_uses_symlink(path, link_error)) {
    report_failure(err, command_name, "refusing symlinked output path", path);
    return false;
  }
  if (link_error) {
    report_failure(err, command_name, "cannot inspect output path", path);
    return false;
  }

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

  if (output_path_uses_symlink(path, link_error)) {
    report_failure(err, command_name, "refusing symlinked output path", path);
    return false;
  }
  if (link_error) {
    report_failure(err, command_name, "cannot inspect output path", path);
    return false;
  }

  const std::filesystem::file_status destination_status =
      std::filesystem::symlink_status(path, link_error);
  if (!link_error && std::filesystem::is_regular_file(destination_status) &&
      std::filesystem::hard_link_count(path, link_error) > 1 &&
      !link_error) {
    report_failure(err, command_name, "refusing multiply-linked output",
                   path);
    return false;
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
