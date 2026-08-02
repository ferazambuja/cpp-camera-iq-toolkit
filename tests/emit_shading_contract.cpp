#include <array>
#include <iostream>
#include <vector>

#include "camera_iq/shading.hpp"

int main() {
  constexpr int bins = 16 * 12;

  camera_iq::ShadingField field;
  field.valid = true;
  field.signal_ceiling_measured = true;
  field.signal_ceiling = {2000.0, 2000.0, 2000.0, 2000.0};
  field.grid_cols = 16;
  field.grid_rows = 12;
  field.geometry.valid = true;
  field.geometry.gate = {0, 0, 800, 800};
  field.geometry.center = {200, 200, 400, 400};
  field.geometry.corners = {{{0, 0, 100, 100}, {700, 0, 100, 100},
                             {0, 700, 100, 100},
                             {700, 700, 100, 100}}};
  field.gates.measured = true;
  field.gates.near_ceiling_ok = true;
  field.gates.screening_coverage_ok = true;
  field.gates.low_signal_ok = true;
  field.gates.negative_ok = true;
  field.gates.coverage_ok = true;
  field.gates.finite_ok = true;
  field.gates.near_ceiling_frac_gate.fill(0.0);
  field.gates.near_ceiling_frac_frame.fill(0.0);
  field.gates.finite_frac_gate.fill(1.0);
  field.gates.finite_frac_frame.fill(1.0);
  field.gates.negative_frac.fill(0.0);
  field.gates.center_signal_frac.fill(0.5);
  field.gates.min_bin_coverage = 1.0;
  field.center_block_median.fill(1000.0);
  for (int position = 0; position < 4; ++position) {
    field.bin_median[position] = std::vector<double>(bins, 1000.0);
    field.relative[position] = std::vector<double>(bins, 1.0);
    for (int corner = 0; corner < 4; ++corner) {
      field.blocks.corner_median[corner][position] = 1000.0;
      field.blocks.corner_relative[corner][position] = 1.0;
    }
  }

  camera_iq::ShadingChromatic chromatic;
  chromatic.layout_valid = true;
  chromatic.valid = true;
  chromatic.complete = true;
  chromatic.red_position = 0;
  chromatic.green1_position = 1;
  chromatic.green2_position = 2;
  chromatic.blue_position = 3;
  chromatic.c_rg = std::vector<double>(bins, 1.0);
  chromatic.c_bg = std::vector<double>(bins, 1.0);
  chromatic.c_g1g2 = std::vector<double>(bins, 1.0);
  chromatic.missing_bin_count = 0;
  chromatic.asymmetry_valid = true;
  chromatic.green_asymmetry = 0.0;
  chromatic.asymmetry_exceeds_policy = false;

  camera_iq::ShadingPedestal pedestal;
  pedestal.measured = true;
  pedestal.compatible = true;
  pedestal.make_model_metadata_matches = true;
  pedestal.body_serials_present = false;
  pedestal.body_serials_match = false;
  pedestal.body_serials_consistent = true;
  pedestal.dark_file = "dataset:fixture/dark.RAF";
  pedestal.residual_dn.fill(0.0);
  pedestal.max_abs_residual_dn = 0.0;
  pedestal.finite_fraction.fill(1.0);
  pedestal.full_finite_coverage = true;
  pedestal.spatial_checked = true;
  pedestal.center_residual_dn.fill(0.0);
  for (auto& corner : pedestal.corner_residual_dn) corner.fill(0.0);
  pedestal.max_abs_spatial_residual_dn = 0.0;
  pedestal.exposure_metadata_present = true;
  pedestal.exposure_metadata_matches = true;
  pedestal.within_tolerance = true;
  pedestal.verified = true;

  camera_iq::write_shading_json(
      std::cout,
      "dataset:fixture/Images/Sphere/Sphere_f8.0_1:1000_DSCF0001.RAF", field,
      chromatic, pedestal);
  return 0;
}
