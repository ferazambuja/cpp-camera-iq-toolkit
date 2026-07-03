#pragma once

#include <optional>
#include <string_view>

namespace camera_iq {

struct RoiRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// Parses "x,y,width,height". Origins must be non-negative and dimensions must
// be positive.
std::optional<RoiRect> parse_roi_spec(std::string_view spec);

// Clips a requested ROI to the image bounds and rounds inward to an even origin
// and even dimensions. The result therefore contains complete active-area CFA
// 2x2 blocks and preserves the global Bayer phase.
std::optional<RoiRect> cfa_balanced_roi(const RoiRect& requested,
                                        int image_width, int image_height);

}  // namespace camera_iq
