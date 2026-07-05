#include "camera_iq/colorimetry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace camera_iq {
namespace {

struct CmfSample {
  double wavelength_nm;
  double x;
  double y;
  double z;
};

// CIE 1931 2 degree standard observer, 10 nm subset from the common
// Wyszecki/Stiles table. Values cover the 2019 CCSG reference axis.
constexpr std::array<CmfSample, 36> kCie1931_2deg = {{
    {380, 0.001368, 0.000039, 0.006450001},
    {390, 0.004243, 0.000120, 0.02005001},
    {400, 0.014310, 0.000396, 0.06785001},
    {410, 0.043510, 0.001210, 0.2074000},
    {420, 0.134380, 0.004000, 0.6456000},
    {430, 0.283900, 0.011600, 1.3856000},
    {440, 0.348280, 0.023000, 1.7470600},
    {450, 0.336200, 0.038000, 1.7721100},
    {460, 0.290800, 0.060000, 1.6692000},
    {470, 0.195360, 0.090980, 1.2876400},
    {480, 0.095640, 0.139020, 0.8129501},
    {490, 0.032010, 0.208020, 0.4651800},
    {500, 0.004900, 0.323000, 0.2720000},
    {510, 0.009300, 0.503000, 0.1582000},
    {520, 0.063270, 0.710000, 0.07824999},
    {530, 0.165500, 0.862000, 0.0421600},
    {540, 0.290400, 0.954000, 0.0203000},
    {550, 0.4334499, 0.9949501, 0.008749999},
    {560, 0.594500, 0.995000, 0.0039000},
    {570, 0.762100, 0.952000, 0.0021000},
    {580, 0.916300, 0.870000, 0.001650001},
    {590, 1.026300, 0.757000, 0.0011000},
    {600, 1.062200, 0.631000, 0.0008000},
    {610, 1.002600, 0.503000, 0.0003400},
    {620, 0.8544499, 0.381000, 0.0001900},
    {630, 0.642400, 0.265000, 0.0000500},
    {640, 0.447900, 0.175000, 0.0000200},
    {650, 0.283500, 0.107000, 0.0000000},
    {660, 0.164900, 0.061000, 0.0000000},
    {670, 0.087400, 0.032000, 0.0000000},
    {680, 0.046770, 0.017000, 0.0000000},
    {690, 0.022700, 0.008210, 0.0000000},
    {700, 0.01135916, 0.004102, 0.0000000},
    {710, 0.005790346, 0.002091, 0.0000000},
    {720, 0.002899327, 0.001047, 0.0000000},
    {730, 0.001439971, 0.000520, 0.0000000},
}};

double interpolate_cmf_component(double wavelength_nm, int component) {
  if (wavelength_nm < kCie1931_2deg.front().wavelength_nm ||
      wavelength_nm > kCie1931_2deg.back().wavelength_nm) {
    throw std::runtime_error("colorimetry: wavelength outside CIE table");
  }
  for (std::size_t i = 0; i < kCie1931_2deg.size(); ++i) {
    if (std::abs(kCie1931_2deg[i].wavelength_nm - wavelength_nm) <= 1e-9) {
      if (component == 0) return kCie1931_2deg[i].x;
      if (component == 1) return kCie1931_2deg[i].y;
      return kCie1931_2deg[i].z;
    }
    if (kCie1931_2deg[i].wavelength_nm > wavelength_nm) {
      const auto& a = kCie1931_2deg[i - 1];
      const auto& b = kCie1931_2deg[i];
      const double t =
          (wavelength_nm - a.wavelength_nm) / (b.wavelength_nm - a.wavelength_nm);
      const double av = component == 0 ? a.x : (component == 1 ? a.y : a.z);
      const double bv = component == 0 ? b.x : (component == 1 ? b.y : b.z);
      return av + (bv - av) * t;
    }
  }
  throw std::runtime_error("colorimetry: wavelength interpolation failed");
}

std::vector<double> integration_weights(const std::vector<double>& wavelengths) {
  if (wavelengths.size() < 2) {
    throw std::runtime_error("colorimetry: at least two wavelengths required");
  }
  std::vector<double> weights(wavelengths.size(), 0.0);
  for (std::size_t i = 0; i < wavelengths.size(); ++i) {
    if (i > 0 && wavelengths[i] <= wavelengths[i - 1]) {
      throw std::runtime_error(
          "colorimetry: wavelengths must be strictly increasing");
    }
    if (i == 0) {
      weights[i] = (wavelengths[i + 1] - wavelengths[i]) * 0.5;
    } else if (i + 1 == wavelengths.size()) {
      weights[i] = (wavelengths[i] - wavelengths[i - 1]) * 0.5;
    } else {
      weights[i] = (wavelengths[i + 1] - wavelengths[i - 1]) * 0.5;
    }
  }
  return weights;
}

Xyz integrate_xyz(const SpectralReference& ref, const std::vector<double>& illum,
                  const std::vector<double>& weights,
                  const std::vector<double>& reflectance, double scale) {
  Xyz out;
  for (std::size_t i = 0; i < ref.wavelengths_nm.size(); ++i) {
    const double e = illum[i];
    const double r = reflectance[i];
    const double w = weights[i];
    out.x += r * e * interpolate_cmf_component(ref.wavelengths_nm[i], 0) * w;
    out.y += r * e * interpolate_cmf_component(ref.wavelengths_nm[i], 1) * w;
    out.z += r * e * interpolate_cmf_component(ref.wavelengths_nm[i], 2) * w;
  }
  out.x *= scale;
  out.y *= scale;
  out.z *= scale;
  return out;
}

double lab_f(double t) {
  constexpr double epsilon = 216.0 / 24389.0;
  constexpr double kappa = 24389.0 / 27.0;
  if (t > epsilon) return std::cbrt(t);
  return (kappa * t + 16.0) / 116.0;
}

double delta_e_76(const Lab& a, const Lab& b) {
  const double dl = a.l - b.l;
  const double da = a.a - b.a;
  const double db = a.b - b.b;
  return std::sqrt(dl * dl + da * da + db * db);
}

std::array<double, 3> solve_3x3(std::array<std::array<double, 3>, 3> a,
                                std::array<double, 3> b) {
  for (std::size_t col = 0; col < 3; ++col) {
    std::size_t pivot = col;
    for (std::size_t row = col + 1; row < 3; ++row) {
      if (std::abs(a[row][col]) > std::abs(a[pivot][col])) pivot = row;
    }
    if (std::abs(a[pivot][col]) < 1e-12) {
      throw std::runtime_error("ccm fit: singular camera design matrix");
    }
    if (pivot != col) {
      std::swap(a[pivot], a[col]);
      std::swap(b[pivot], b[col]);
    }
    const double denom = a[col][col];
    for (std::size_t j = col; j < 3; ++j) a[col][j] /= denom;
    b[col] /= denom;
    for (std::size_t row = 0; row < 3; ++row) {
      if (row == col) continue;
      const double factor = a[row][col];
      for (std::size_t j = col; j < 3; ++j) {
        a[row][j] -= factor * a[col][j];
      }
      b[row] -= factor * b[col];
    }
  }
  return b;
}

std::array<double, 3> rgb_array(const CameraRgbPatch& p) {
  return {p.r, p.g, p.b};
}

std::string trim_cr(std::string s) {
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

bool parse_numeric_pair(std::string_view line, double& a, double& b) {
  const std::size_t comma = line.find(',');
  if (comma == std::string_view::npos) return false;
  try {
    const std::string first(line.substr(0, comma));
    const std::string second(line.substr(comma + 1));
    std::size_t consumed_a = 0;
    std::size_t consumed_b = 0;
    a = std::stod(first, &consumed_a);
    b = std::stod(second, &consumed_b);
    if (consumed_a != first.size() || consumed_b != second.size()) {
      return false;
    }
    return std::isfinite(a) && std::isfinite(b);
  } catch (...) {
    return false;
  }
}

}  // namespace

std::vector<double> read_spectrum_csv_interpolated(
    const std::filesystem::path& path,
    const std::vector<double>& target_wavelengths_nm) {
  std::ifstream is(path, std::ios::binary);
  if (!is) {
    throw std::runtime_error("spectrum CSV: cannot open " + path.string());
  }

  std::vector<double> wavelengths;
  std::vector<double> values;
  std::string line;
  while (std::getline(is, line)) {
    line = trim_cr(line);
    if (line.empty()) continue;
    double wavelength_nm = 0;
    double value = 0;
    if (!parse_numeric_pair(line, wavelength_nm, value)) continue;
    if (!wavelengths.empty() && wavelength_nm <= wavelengths.back()) {
      throw std::runtime_error(
          "spectrum CSV: wavelengths must be strictly increasing");
    }
    wavelengths.push_back(wavelength_nm);
    values.push_back(value);
  }
  if (wavelengths.size() < 2) {
    throw std::runtime_error(
        "spectrum CSV: at least two numeric wavelength rows required");
  }

  std::vector<double> out;
  out.reserve(target_wavelengths_nm.size());
  for (const double target : target_wavelengths_nm) {
    if (!std::isfinite(target)) {
      throw std::runtime_error("spectrum CSV: target wavelength is not finite");
    }
    if (target < wavelengths.front() || target > wavelengths.back()) {
      throw std::runtime_error(
          "spectrum CSV: target wavelength outside source range");
    }
    const auto upper =
        std::lower_bound(wavelengths.begin(), wavelengths.end(), target);
    if (upper == wavelengths.begin()) {
      if (values.front() < 0) {
        throw std::runtime_error(
            "spectrum CSV: interpolated target value is negative");
      }
      out.push_back(values.front());
      continue;
    }
    if (upper != wavelengths.end() && std::abs(*upper - target) <= 1e-9) {
      const double value =
          values[static_cast<std::size_t>(
              std::distance(wavelengths.begin(), upper))];
      if (value < 0) {
        throw std::runtime_error(
            "spectrum CSV: interpolated target value is negative");
      }
      out.push_back(value);
      continue;
    }
    const std::size_t hi =
        static_cast<std::size_t>(std::distance(wavelengths.begin(), upper));
    const std::size_t lo = hi - 1;
    const double t =
        (target - wavelengths[lo]) / (wavelengths[hi] - wavelengths[lo]);
    const double value = values[lo] + (values[hi] - values[lo]) * t;
    if (value < 0) {
      throw std::runtime_error(
          "spectrum CSV: interpolated target value is negative");
    }
    out.push_back(value);
  }
  return out;
}

RenderedReference render_reference_xyz(const SpectralReference& ref,
                                       const std::vector<double>& illuminant) {
  if (ref.wavelengths_nm.size() != illuminant.size()) {
    throw std::runtime_error(
        "colorimetry: illuminant and reference wavelength counts differ");
  }
  for (const double v : illuminant) {
    if (!std::isfinite(v) || v < 0) {
      throw std::runtime_error(
          "colorimetry: illuminant values must be finite and non-negative");
    }
  }
  const auto weights = integration_weights(ref.wavelengths_nm);
  std::vector<double> perfect(ref.wavelengths_nm.size(), 1.0);
  const Xyz unscaled_white =
      integrate_xyz(ref, illuminant, weights, perfect, 1.0);
  if (unscaled_white.y <= 0) {
    throw std::runtime_error("colorimetry: illuminant has zero Y response");
  }
  const double scale = 100.0 / unscaled_white.y;

  RenderedReference out;
  out.white_xyz = integrate_xyz(ref, illuminant, weights, perfect, scale);
  out.patch_xyz.reserve(ref.patches.size());
  for (const auto& patch : ref.patches) {
    if (patch.reflectance.size() != ref.wavelengths_nm.size()) {
      throw std::runtime_error("colorimetry: patch reflectance width mismatch");
    }
    out.patch_xyz.push_back(
        integrate_xyz(ref, illuminant, weights, patch.reflectance, scale));
  }
  return out;
}

Lab xyz_to_lab(const Xyz& xyz, const Xyz& white) {
  if (white.x <= 0 || white.y <= 0 || white.z <= 0) {
    throw std::runtime_error("Lab: white XYZ must be positive");
  }
  const double fx = lab_f(xyz.x / white.x);
  const double fy = lab_f(xyz.y / white.y);
  const double fz = lab_f(xyz.z / white.z);
  Lab out;
  out.l = 116.0 * fy - 16.0;
  out.a = 500.0 * (fx - fy);
  out.b = 200.0 * (fy - fz);
  return out;
}

Xyz apply_ccm(const std::array<std::array<double, 3>, 3>& matrix,
              const CameraRgbPatch& rgb) {
  const auto r = rgb_array(rgb);
  Xyz out;
  out.x = matrix[0][0] * r[0] + matrix[0][1] * r[1] + matrix[0][2] * r[2];
  out.y = matrix[1][0] * r[0] + matrix[1][1] * r[1] + matrix[1][2] * r[2];
  out.z = matrix[2][0] * r[0] + matrix[2][1] * r[1] + matrix[2][2] * r[2];
  return out;
}

CcmFit fit_rgb_to_xyz_ccm(const std::vector<CameraRgbPatch>& camera_rgb,
                          const std::vector<Xyz>& target_xyz,
                          const Xyz& white_xyz) {
  if (camera_rgb.size() != target_xyz.size()) {
    throw std::runtime_error("ccm fit: camera and target patch counts differ");
  }
  if (camera_rgb.size() < 3) {
    throw std::runtime_error("ccm fit: at least three patches required");
  }

  std::array<std::array<double, 3>, 3> normal{};
  std::array<std::array<double, 3>, 3> rhs{};
  for (std::size_t i = 0; i < camera_rgb.size(); ++i) {
    const auto rgb = rgb_array(camera_rgb[i]);
    const std::array<double, 3> xyz = {
        target_xyz[i].x, target_xyz[i].y, target_xyz[i].z};
    for (std::size_t row = 0; row < 3; ++row) {
      for (std::size_t col = 0; col < 3; ++col) {
        normal[row][col] += rgb[row] * rgb[col];
      }
      for (std::size_t channel = 0; channel < 3; ++channel) {
        rhs[channel][row] += xyz[channel] * rgb[row];
      }
    }
  }

  CcmFit fit;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    fit.matrix[channel] = solve_3x3(normal, rhs[channel]);
  }
  fit.patch_count = camera_rgb.size();

  double sum = 0;
  double sumsq = 0;
  for (std::size_t i = 0; i < camera_rgb.size(); ++i) {
    const Lab target_lab = xyz_to_lab(target_xyz[i], white_xyz);
    const Lab predicted_lab = xyz_to_lab(apply_ccm(fit.matrix, camera_rgb[i]),
                                         white_xyz);
    const double de = delta_e_76(target_lab, predicted_lab);
    sum += de;
    sumsq += de * de;
    fit.max_delta_e_76 = std::max(fit.max_delta_e_76, de);
  }
  fit.mean_delta_e_76 = sum / static_cast<double>(camera_rgb.size());
  fit.rms_delta_e_76 =
      std::sqrt(sumsq / static_cast<double>(camera_rgb.size()));
  return fit;
}

}  // namespace camera_iq
