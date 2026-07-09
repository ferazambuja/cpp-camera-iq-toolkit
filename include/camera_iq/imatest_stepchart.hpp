#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace camera_iq {

struct ImatestStepchartZone {
  int zone = 0;
  double pixel = 0.0;
  double pixel_255 = 0.0;
  double log_exposure = 0.0;
  double log_pixel_255 = 0.0;
  int width_px = 0;
  int height_px = 0;
  int pixels_total = 0;
};

struct ImatestStepchartSummary {
  std::string imatest_version;
  std::string run_date;
  std::string summary_file_basename;
  std::string file_name;
  std::vector<std::string> combined_files;
  int declared_file_count = 0;
  int declared_zone_count = 0;
  std::vector<ImatestStepchartZone> zones;
};

ImatestStepchartSummary read_imatest_stepchart_summary(
    const std::filesystem::path& path);

}  // namespace camera_iq
