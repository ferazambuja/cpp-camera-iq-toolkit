#include "camera_iq/spectral_reference_audit.hpp"

#include "camera_iq/colorimetry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace camera_iq {

SpectralReferenceColorimetryAudit audit_spectral_reference_colorimetry(
    const SpectralReference& reference,
    const std::vector<double>& illuminant,
    const SpectroCmfTable& observer,
    std::string illuminant_label,
    std::string observer_label) {
  if (illuminant_label.empty() || observer_label.empty()) {
    throw std::runtime_error(
        "spectral reference audit: illuminant and observer labels are required");
  }
  const auto rendered =
      render_reference_xyz(reference, illuminant, observer);
  SpectralReferenceColorimetryAudit result;
  result.illuminant = std::move(illuminant_label);
  result.observer = std::move(observer_label);
  if (reference.cgats_schema) {
    result.observer_metadata_conflict =
        reference.cgats_schema->observer_declarations_conflict;
    result.source_observer_declarations =
        reference.cgats_schema->observer_declarations;
  }

  double delta_sum = 0.0;
  double xyz_relative_sum = 0.0;
  for (std::size_t index = 0; index < reference.patches.size(); ++index) {
    const auto& patch = reference.patches[index];
    if (patch.declared_lab) {
      const Lab declared{(*patch.declared_lab)[0], (*patch.declared_lab)[1],
                         (*patch.declared_lab)[2]};
      const double delta =
          delta_e_76(declared,
                     xyz_to_lab(rendered.patch_xyz[index], rendered.white_xyz));
      if (!std::isfinite(delta)) {
        throw std::runtime_error(
            "spectral reference audit: Delta E 76 is not finite");
      }
      delta_sum += delta;
      result.max_delta_e_76 = std::max(result.max_delta_e_76, delta);
      ++result.lab_patch_count;
    }
    if (patch.declared_xyz) {
      const auto& declared = *patch.declared_xyz;
      const auto& computed = rendered.patch_xyz[index];
      const double norm =
          std::hypot(std::hypot(declared[0], declared[1]), declared[2]);
      if (!std::isfinite(norm) || norm <= 0.0) {
        throw std::runtime_error(
            "spectral reference audit: declared XYZ norm must be positive");
      }
      const double residual = std::hypot(
          std::hypot(computed.x - declared[0], computed.y - declared[1]),
          computed.z - declared[2]);
      const double relative = residual / norm;
      if (!std::isfinite(relative)) {
        throw std::runtime_error(
            "spectral reference audit: XYZ residual is not finite");
      }
      xyz_relative_sum += relative;
      result.max_xyz_relative_l2 =
          std::max(result.max_xyz_relative_l2, relative);
      ++result.xyz_patch_count;
    }
  }
  if (result.lab_patch_count > 0) {
    result.mean_delta_e_76 =
        delta_sum / static_cast<double>(result.lab_patch_count);
  }
  if (result.xyz_patch_count > 0) {
    result.mean_xyz_relative_l2 =
        xyz_relative_sum / static_cast<double>(result.xyz_patch_count);
  }
  return result;
}

SpectralReferenceRepeatAudit audit_spectral_reference_repeat(
    const SpectralReference& first, const SpectralReference& second,
    const std::vector<double>& illuminant, const SpectroCmfTable& observer,
    std::string illuminant_label, std::string observer_label) {
  if (first.wavelengths_nm != second.wavelengths_nm ||
      first.patches.size() != second.patches.size() || first.patches.empty()) {
    throw std::runtime_error(
        "spectral reference repeat: references must share a non-empty layout and axis");
  }
  const auto first_rendered =
      render_reference_xyz(first, illuminant, observer);
  const auto second_rendered =
      render_reference_xyz(second, illuminant, observer);
  SpectralReferenceRepeatAudit result;
  result.illuminant = std::move(illuminant_label);
  result.observer = std::move(observer_label);
  result.patch_count = first.patches.size();
  result.patches.reserve(first.patches.size());
  double rms_sum = 0.0;
  double delta_sum = 0.0;
  for (std::size_t patch_index = 0; patch_index < first.patches.size();
       ++patch_index) {
    const auto& a = first.patches[patch_index];
    const auto& b = second.patches[patch_index];
    if (a.id != b.id || a.reflectance.size() != b.reflectance.size() ||
        a.reflectance.empty()) {
      throw std::runtime_error(
          "spectral reference repeat: patch identities or widths differ");
    }
    double squared_sum = 0.0;
    for (std::size_t band = 0; band < a.reflectance.size(); ++band) {
      const double difference = a.reflectance[band] - b.reflectance[band];
      squared_sum += difference * difference;
    }
    const double rms =
        std::sqrt(squared_sum / static_cast<double>(a.reflectance.size()));
    const double delta = delta_e_76(
        xyz_to_lab(first_rendered.patch_xyz[patch_index],
                   first_rendered.white_xyz),
        xyz_to_lab(second_rendered.patch_xyz[patch_index],
                   second_rendered.white_xyz));
    if (!std::isfinite(rms) || !std::isfinite(delta)) {
      throw std::runtime_error(
          "spectral reference repeat: residual is not finite");
    }
    rms_sum += rms;
    delta_sum += delta;
    result.max_reflectance_rms = std::max(result.max_reflectance_rms, rms);
    result.max_delta_e_76 = std::max(result.max_delta_e_76, delta);
    result.patches.push_back({a.id, rms, delta});
  }
  result.mean_reflectance_rms = rms_sum / static_cast<double>(result.patch_count);
  result.mean_delta_e_76 = delta_sum / static_cast<double>(result.patch_count);
  return result;
}

}  // namespace camera_iq
