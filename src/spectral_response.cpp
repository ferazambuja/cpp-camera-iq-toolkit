#include "camera_iq/spectral_response.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "camera_iq/cfa_stats.hpp"
#include "camera_iq/json_writer.hpp"

namespace camera_iq {
namespace {

constexpr std::size_t kExpectedSamples = 48;
constexpr int kFirstWavelengthNm = 360;
constexpr int kLastWavelengthNm = 830;
constexpr int kWavelengthStepNm = 10;
constexpr std::string_view kLegacyNormalization =
    "legacy_peak_channel_normalized_green_1_no_rescale";
constexpr std::string_view kLegacyValidationTier = "legacy_fidelity_only";
constexpr std::string_view kLegacySource = "legacy_bobby_gold_csv";
constexpr std::string_view kToolkitRawSource = "toolkit_raw_extraction";
constexpr std::string_view kToolkitRawNormalization =
    "toolkit_raw_peak_green_1";

std::string trim_cr(std::string s) {
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

std::vector<std::string> parse_csv_line(std::string_view line) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char c = line[i];
    if (quoted) {
      if (c == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          field += '"';
          ++i;
        } else {
          quoted = false;
        }
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      fields.push_back(field);
      field.clear();
    } else {
      field += c;
    }
  }
  if (quoted) {
    throw std::runtime_error("spectral response CSV: unterminated quote");
  }
  fields.push_back(field);
  return fields;
}

double parse_double_field(const std::string& field,
                          const std::string& context) {
  try {
    std::size_t consumed = 0;
    const double value = std::stod(field, &consumed);
    if (consumed != field.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (...) {
    throw std::runtime_error("spectral response CSV: invalid number in " +
                             context + ": '" + field + "'");
  }
}

int rounded_10nm_axis(double wavelength_nm, const std::string& context) {
  if (!std::isfinite(wavelength_nm)) {
    throw std::runtime_error("spectral response CSV: non-finite wavelength in " +
                             context);
  }
  return static_cast<int>(std::llround(wavelength_nm / 10.0) * 10);
}

int expected_axis_at(std::size_t index) {
  return kFirstWavelengthNm + static_cast<int>(index) * kWavelengthStepNm;
}

void validate_axis_sample(std::size_t index, int rounded_nm,
                          const std::string& context) {
  const int expected = expected_axis_at(index);
  if (rounded_nm != expected) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " wavelength axis mismatch at row " +
                             std::to_string(index + 1) + " (expected " +
                             std::to_string(expected) + " nm after rounding)");
  }
}

void validate_axis_vector(const std::vector<int>& axis,
                          const std::string& context) {
  if (axis.size() != kExpectedSamples) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " expected 48 samples, got " +
                             std::to_string(axis.size()));
  }
  for (std::size_t i = 0; i < axis.size(); ++i) {
    validate_axis_sample(i, axis[i], context);
    if (i > 0 && axis[i] - axis[i - 1] != kWavelengthStepNm) {
      throw std::runtime_error("spectral response CSV: " + context +
                               " wavelength axis is not strictly 10 nm");
    }
  }
  if (axis.front() != kFirstWavelengthNm || axis.back() != kLastWavelengthNm) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " wavelength endpoints are not 360/830 nm");
  }
}

void require_width(const std::vector<std::string>& fields, std::size_t width,
                   const std::string& context) {
  if (fields.size() != width) {
    throw std::runtime_error("spectral response CSV: " + context +
                             " expected " + std::to_string(width) +
                             " fields, got " +
                             std::to_string(fields.size()));
  }
}

void write_double_array(JsonWriter& w, const std::vector<double>& values) {
  w.begin_array();
  for (const double value : values) w.value(value);
  w.end_array();
}

void write_int_array(JsonWriter& w, const std::vector<int>& values) {
  w.begin_array();
  for (const int value : values) w.value(value);
  w.end_array();
}

struct PositionMeans {
  std::array<double, 4> mean{0, 0, 0, 0};
  std::array<std::size_t, 4> count{0, 0, 0, 0};
  std::array<std::size_t, 4> saturated{0, 0, 0, 0};
};

struct ChannelSignals {
  double r = 0;
  double g = 0;
  double b = 0;
  double saturated_fraction_r = 0;
  double saturated_fraction_g = 0;
  double saturated_fraction_b = 0;
  double below_dark_fraction_r = 0;
  double below_dark_fraction_g = 0;
  double below_dark_fraction_b = 0;
};

void validate_response_vectors(const SpectralResponse& response,
                               const std::string& context) {
  if (response.axis_nm.size() != kExpectedSamples ||
      response.response_r.size() != kExpectedSamples ||
      response.response_g.size() != kExpectedSamples ||
      response.response_b.size() != kExpectedSamples ||
      response.line_spd.size() != kExpectedSamples) {
    throw std::runtime_error("spectral response: " + context +
                             " must have 48 aligned samples");
  }
}

RoiRect default_central_roi(const RawCfaImage& image) {
  const int width = image.width / 2;
  const int height = image.height / 2;
  return RoiRect{image.width / 4, image.height / 4, width, height};
}

PositionMeans residual_means_for_roi(const RawCfaImage& image, RoiRect roi,
                                     double near_saturation_fraction,
                                     bool reject_saturation) {
  const auto actual = cfa_balanced_roi(roi, image.width, image.height);
  if (!actual) {
    throw std::runtime_error("spectral response RAW: invalid measurement ROI");
  }
  if (image.row_stride_pixels < image.width ||
      image.samples.size() <
          static_cast<std::size_t>(image.row_stride_pixels) *
              static_cast<std::size_t>(image.height)) {
    throw std::runtime_error("spectral response RAW: malformed CFA image");
  }

  struct Acc {
    double mean = 0;
    std::size_t count = 0;
    std::size_t saturated = 0;
  };
  std::array<Acc, 4> acc;
  const double sat_threshold = image.meta.white_level * near_saturation_fraction;
  for (int r = actual->y; r < actual->y + actual->height; ++r) {
    const std::size_t row = static_cast<std::size_t>(r) *
                            static_cast<std::size_t>(image.row_stride_pixels);
    for (int c = actual->x; c < actual->x + actual->width; ++c) {
      const std::size_t pos = static_cast<std::size_t>((r & 1) * 2 + (c & 1));
      const double residual = image.samples[row + static_cast<std::size_t>(c)];
      const double raw = residual + image.meta.black_per_channel[pos];
      Acc& a = acc[pos];
      if (raw >= sat_threshold) {
        ++a.saturated;
        if (reject_saturation) continue;
      }
      ++a.count;
      const double delta = residual - a.mean;
      a.mean += delta / static_cast<double>(a.count);
    }
  }

  PositionMeans out;
  for (std::size_t i = 0; i < 4; ++i) {
    if (acc[i].count == 0) {
      throw std::runtime_error(
          "spectral response RAW: no unsaturated samples for a CFA position");
    }
    out.mean[i] = acc[i].mean;
    out.count[i] = acc[i].count;
    out.saturated[i] = acc[i].saturated;
  }
  return out;
}

ChannelSignals combine_position_signals(
    const RawCfaImage& image, const PositionMeans& light,
    const std::array<double, 4>& dark_means) {
  const auto labels = channel_labels(image.cdesc, image.color_at_position);
  struct Acc {
    double weighted_sum = 0;
    double saturated = 0;
    double below_dark = 0;
    double signal_count = 0;
    double total_count = 0;
  };
  Acc r, g, b;
  for (std::size_t p = 0; p < 4; ++p) {
    const double signal = light.mean[p] - dark_means[p];
    Acc* acc = nullptr;
    if (!labels[p].empty() && labels[p][0] == 'R') {
      acc = &r;
    } else if (!labels[p].empty() && labels[p][0] == 'G') {
      acc = &g;
    } else if (!labels[p].empty() && labels[p][0] == 'B') {
      acc = &b;
    }
    if (acc == nullptr) {
      throw std::runtime_error("spectral response RAW: unsupported CFA label");
    }
    const double count = static_cast<double>(light.count[p]);
    const double total =
        count + static_cast<double>(light.saturated[p]);
    acc->weighted_sum += std::max(0.0, signal) * count;
    acc->saturated += static_cast<double>(light.saturated[p]);
    if (signal <= 0.0) acc->below_dark += count;
    acc->signal_count += count;
    acc->total_count += total;
  }
  if (r.signal_count <= 0 || g.signal_count <= 0 || b.signal_count <= 0) {
    throw std::runtime_error("spectral response RAW: missing RGB CFA channels");
  }
  return ChannelSignals{
      r.weighted_sum / r.signal_count,
      g.weighted_sum / g.signal_count,
      b.weighted_sum / b.signal_count,
      r.total_count > 0 ? r.saturated / r.total_count : 0.0,
      g.total_count > 0 ? g.saturated / g.total_count : 0.0,
      b.total_count > 0 ? b.saturated / b.total_count : 0.0,
      r.signal_count > 0 ? r.below_dark / r.signal_count : 0.0,
      g.signal_count > 0 ? g.below_dark / g.signal_count : 0.0,
      b.signal_count > 0 ? b.below_dark / b.signal_count : 0.0};
}

double pearson(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size() || a.size() < 2) {
    throw std::runtime_error("spectral response: correlation vector mismatch");
  }
  double ma = 0;
  double mb = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    ma += a[i];
    mb += b[i];
  }
  ma /= static_cast<double>(a.size());
  mb /= static_cast<double>(b.size());
  double cov = 0;
  double va = 0;
  double vb = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double da = a[i] - ma;
    const double db = b[i] - mb;
    cov += da * db;
    va += da * da;
    vb += db * db;
  }
  if (va <= 0 || vb <= 0) {
    throw std::runtime_error("spectral response: zero-variance fidelity curve");
  }
  return cov / std::sqrt(va * vb);
}

double rms_error(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size() || a.empty()) {
    throw std::runtime_error("spectral response: RMS vector mismatch");
  }
  double sum = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double d = a[i] - b[i];
    sum += d * d;
  }
  return std::sqrt(sum / static_cast<double>(a.size()));
}

SpectralFidelityChannel compare_channel(const std::vector<double>& extracted,
                                        const std::vector<double>& legacy) {
  return SpectralFidelityChannel{rms_error(extracted, legacy),
                                 pearson(extracted, legacy)};
}

std::optional<int> trailing_frame_number(const std::filesystem::path& path) {
  const std::string stem = path.stem().string();
  std::size_t first_digit = stem.size();
  while (first_digit > 0) {
    const char c = stem[first_digit - 1];
    if (c < '0' || c > '9') break;
    --first_digit;
  }
  if (first_digit == stem.size()) return std::nullopt;
  const std::string suffix = stem.substr(first_digit);
  return std::stoi(suffix);
}

}  // namespace

SpectralResponse normalize_spectral_response(SpectralResponse response) {
  if (response.axis_nm.size() != kExpectedSamples ||
      response.response_r.size() != kExpectedSamples ||
      response.response_g.size() != kExpectedSamples ||
      response.response_b.size() != kExpectedSamples ||
      response.line_spd.size() != kExpectedSamples) {
    throw std::runtime_error(
        "spectral response CSV: cannot normalize incomplete response");
  }

  double max_r = -std::numeric_limits<double>::infinity();
  double max_g = -std::numeric_limits<double>::infinity();
  double max_b = -std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i < kExpectedSamples; ++i) {
    max_r = std::max(max_r, response.response_r[i]);
    max_g = std::max(max_g, response.response_g[i]);
    max_b = std::max(max_b, response.response_b[i]);
  }

  constexpr double tol = 1e-9;
  const double global_max = std::max({max_r, max_g, max_b});
  if (std::abs(max_g - 1.0) > tol || std::abs(global_max - 1.0) > tol) {
    throw std::runtime_error(
        "spectral response CSV: legacy response is not green-peak normalized "
        "to 1.0");
  }

  response.normalization = std::string(kLegacyNormalization);
  response.validation_tier = std::string(kLegacyValidationTier);
  if (response.source.empty()) response.source = std::string(kLegacySource);
  return response;
}

SpectralResponse parse_spectral_response(
    const std::filesystem::path& legacy_response_csv,
    const std::filesystem::path& line_spd_csv,
    SpectralResponseProvenance provenance) {
  SpectralResponse response;
  response.camera_model = provenance.camera_model;
  response.dataset_id = provenance.dataset_id;
  response.archive_subset = provenance.archive_subset;
  response.source = std::string(kLegacySource);

  std::ifstream legacy(legacy_response_csv, std::ios::binary);
  if (!legacy) {
    throw std::runtime_error("spectral response CSV: cannot open " +
                             legacy_response_csv.string());
  }
  std::string line;
  if (!std::getline(legacy, line)) {
    throw std::runtime_error("spectral response CSV: empty legacy response");
  }
  const auto header = parse_csv_line(trim_cr(line));
  require_width(header, 4, "legacy response header");
  if (header[0] != "Wavelength (nm)" || header[1] != "Red" ||
      header[2] != "Green" || header[3] != "Blue") {
    throw std::runtime_error(
        "spectral response CSV: unexpected legacy response header");
  }

  while (std::getline(legacy, line)) {
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    require_width(fields, 4, "legacy response row");
    const std::size_t index = response.axis_nm.size();
    const int rounded_nm = rounded_10nm_axis(
        parse_double_field(fields[0], "legacy response wavelength"),
        "legacy response");
    response.axis_nm.push_back(rounded_nm);
    response.response_r.push_back(
        parse_double_field(fields[1], "legacy response red"));
    response.response_g.push_back(
        parse_double_field(fields[2], "legacy response green"));
    response.response_b.push_back(
        parse_double_field(fields[3], "legacy response blue"));
    validate_axis_sample(index, rounded_nm, "legacy response");
  }
  validate_axis_vector(response.axis_nm, "legacy response");

  std::ifstream spd(line_spd_csv, std::ios::binary);
  if (!spd) {
    throw std::runtime_error("spectral response CSV: cannot open " +
                             line_spd_csv.string());
  }
  if (!std::getline(spd, line)) {
    throw std::runtime_error("spectral response CSV: empty line SPD");
  }
  const auto spd_header = parse_csv_line(trim_cr(line));
  require_width(spd_header, 2, "line SPD header");

  std::vector<int> spd_axis;
  while (std::getline(spd, line)) {
    line = trim_cr(line);
    if (line.empty()) continue;
    const auto fields = parse_csv_line(line);
    require_width(fields, 2, "line SPD row");
    const std::size_t index = spd_axis.size();
    const int rounded_nm = rounded_10nm_axis(
        parse_double_field(fields[0], "line SPD wavelength"), "line SPD");
    validate_axis_sample(index, rounded_nm, "line SPD");
    spd_axis.push_back(rounded_nm);
    const double voltage = parse_double_field(fields[1], "line SPD voltage");
    if (voltage <= 0.0) {
      throw std::runtime_error(
          "spectral response CSV: line SPD voltage must be > 0");
    }
    response.line_spd.push_back(voltage);
  }
  validate_axis_vector(spd_axis, "line SPD");
  if (spd_axis != response.axis_nm) {
    throw std::runtime_error(
        "spectral response CSV: response and line SPD axes differ");
  }

  return normalize_spectral_response(std::move(response));
}

std::vector<std::filesystem::path> discover_spectral_sweep_files(
    const std::filesystem::path& raw_dir, const std::vector<int>& axis_nm) {
  if (axis_nm.size() != kExpectedSamples) {
    throw std::runtime_error(
        "spectral response RAW: wavelength axis must have 48 samples");
  }
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(raw_dir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() == ".CR2") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end());
  if (files.size() != axis_nm.size()) {
    throw std::runtime_error("spectral response RAW: expected " +
                             std::to_string(axis_nm.size()) +
                             " sweep RAW files, got " +
                             std::to_string(files.size()));
  }
  std::optional<int> first;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const auto frame = trailing_frame_number(files[i]);
    if (!frame) {
      throw std::runtime_error(
          "spectral response RAW: sweep filename lacks trailing frame number");
    }
    if (i == 0) first = *frame;
    const int expected = *first + static_cast<int>(i);
    if (*frame != expected) {
      throw std::runtime_error(
          "spectral response RAW: sweep filenames are not contiguous");
    }
  }
  return files;
}

SpectralRawExtraction extract_raw_spectral_response(
    const SpectralResponse& legacy,
    const std::vector<RawCfaImage>& sweep_images, const RawCfaImage& dark_image,
    RoiRect requested_roi, double near_saturation_fraction,
    std::vector<std::filesystem::path> raw_paths) {
  validate_response_vectors(legacy, "legacy response");
  if (sweep_images.size() != legacy.axis_nm.size()) {
    throw std::runtime_error(
        "spectral response RAW: sweep-count and wavelength-count mismatch");
  }
  if (!raw_paths.empty() && raw_paths.size() != sweep_images.size()) {
    throw std::runtime_error(
        "spectral response RAW: raw path count and sweep count mismatch");
  }
  if (near_saturation_fraction <= 0.0 || near_saturation_fraction > 1.0) {
    throw std::runtime_error(
        "spectral response RAW: near-saturation fraction must be in (0,1]");
  }
  if (requested_roi.width <= 0 || requested_roi.height <= 0) {
    requested_roi = default_central_roi(sweep_images.front());
  }

  SpectralRawExtraction out;
  out.response.camera_model = legacy.camera_model;
  out.response.dataset_id = legacy.dataset_id;
  out.response.archive_subset = legacy.archive_subset;
  out.response.source = std::string(kToolkitRawSource);
  out.response.axis_nm = legacy.axis_nm;
  out.response.line_spd = legacy.line_spd;
  out.response.normalization = std::string(kToolkitRawNormalization);
  out.response.validation_tier = std::string(kLegacyValidationTier);
  out.near_saturation_fraction = near_saturation_fraction;
  out.metadata_black_by_position = dark_image.meta.black_per_channel;

  const auto actual_roi =
      cfa_balanced_roi(requested_roi, dark_image.width, dark_image.height);
  if (!actual_roi) {
    throw std::runtime_error("spectral response RAW: invalid dark-frame ROI");
  }
  out.measurement_roi = *actual_roi;

  const auto dark_stats =
      residual_means_for_roi(dark_image, out.measurement_roi,
                             near_saturation_fraction, false);
  out.dark_residual_mean_by_position = dark_stats.mean;

  std::vector<double> raw_r, raw_g, raw_b;
  raw_r.reserve(kExpectedSamples);
  raw_g.reserve(kExpectedSamples);
  raw_b.reserve(kExpectedSamples);
  out.samples.reserve(kExpectedSamples);

  for (std::size_t i = 0; i < sweep_images.size(); ++i) {
    const auto& image = sweep_images[i];
    if (image.width != dark_image.width || image.height != dark_image.height ||
        image.cdesc != dark_image.cdesc ||
        image.color_at_position != dark_image.color_at_position) {
      throw std::runtime_error(
          "spectral response RAW: sweep and dark frame geometry differ");
    }
    const auto light_stats =
        residual_means_for_roi(image, out.measurement_roi,
                               near_saturation_fraction, true);
    const ChannelSignals signals =
        combine_position_signals(image, light_stats,
                                 out.dark_residual_mean_by_position);
    const double spd = legacy.line_spd[i];
    if (spd <= 0.0 || !std::isfinite(spd)) {
      throw std::runtime_error("spectral response RAW: invalid line SPD");
    }
    raw_r.push_back(signals.r / spd);
    raw_g.push_back(signals.g / spd);
    raw_b.push_back(signals.b / spd);

    SpectralSampleDiagnostics diag;
    diag.wavelength_nm = legacy.axis_nm[i];
    if (!raw_paths.empty()) diag.raw_path = raw_paths[i];
    diag.mean_signal_r = signals.r;
    diag.mean_signal_g = signals.g;
    diag.mean_signal_b = signals.b;
    diag.saturated_fraction_r = signals.saturated_fraction_r;
    diag.saturated_fraction_g = signals.saturated_fraction_g;
    diag.saturated_fraction_b = signals.saturated_fraction_b;
    diag.below_dark_fraction_r = signals.below_dark_fraction_r;
    diag.below_dark_fraction_g = signals.below_dark_fraction_g;
    diag.below_dark_fraction_b = signals.below_dark_fraction_b;
    out.samples.push_back(diag);
  }

  const double max_g = *std::max_element(raw_g.begin(), raw_g.end());
  if (max_g <= 0.0 || !std::isfinite(max_g)) {
    throw std::runtime_error(
        "spectral response RAW: extracted green response has no positive peak");
  }
  for (std::size_t i = 0; i < raw_g.size(); ++i) {
    out.response.response_r.push_back(raw_r[i] / max_g);
    out.response.response_g.push_back(raw_g[i] / max_g);
    out.response.response_b.push_back(raw_b[i] / max_g);
  }

  out.tier1_legacy_fidelity.r =
      compare_channel(out.response.response_r, legacy.response_r);
  out.tier1_legacy_fidelity.g =
      compare_channel(out.response.response_g, legacy.response_g);
  out.tier1_legacy_fidelity.b =
      compare_channel(out.response.response_b, legacy.response_b);
  return out;
}

SpectralRawExtraction extract_raw_spectral_response_from_files(
    const SpectralResponse& legacy, const std::filesystem::path& raw_dir,
    const std::filesystem::path& dark_raw, RoiRect requested_roi,
    double near_saturation_fraction) {
  const auto raw_paths = discover_spectral_sweep_files(raw_dir, legacy.axis_nm);
  const auto dark_image = read_raw_cfa_image(dark_raw);
  if (!dark_image) {
    throw std::runtime_error("spectral response RAW: cannot read dark RAW");
  }
  std::vector<RawCfaImage> images;
  images.reserve(raw_paths.size());
  for (const auto& path : raw_paths) {
    auto image = read_raw_cfa_image(path);
    if (!image) {
      throw std::runtime_error("spectral response RAW: cannot read " +
                               path.generic_string());
    }
    images.push_back(std::move(*image));
  }
  return extract_raw_spectral_response(legacy, images, *dark_image,
                                       requested_roi, near_saturation_fraction,
                                       raw_paths);
}

namespace {

void write_fidelity_channel(JsonWriter& w,
                            const SpectralFidelityChannel& channel) {
  w.begin_object();
  w.key("rms");
  w.value(channel.rms);
  w.key("correlation");
  w.value(channel.correlation);
  w.end_object();
}

void write_roi(JsonWriter& w, const RoiRect& roi) {
  w.begin_object();
  w.key("x");
  w.value(roi.x);
  w.key("y");
  w.value(roi.y);
  w.key("width");
  w.value(roi.width);
  w.key("height");
  w.value(roi.height);
  w.end_object();
}

void write_position_array(JsonWriter& w, const std::array<double, 4>& values) {
  w.begin_array();
  for (const double value : values) w.value(value);
  w.end_array();
}

void write_response_object(JsonWriter& w, const SpectralResponse& response) {
  w.begin_object();
  w.key("camera_model");
  w.value(response.camera_model);
  w.key("dataset_id");
  w.value(response.dataset_id);
  w.key("archive_subset");
  w.value(response.archive_subset);
  w.key("source");
  w.value(response.source);
  w.key("axis_nm");
  write_int_array(w, response.axis_nm);
  w.key("response_rgb");
  w.begin_object();
  w.key("r");
  write_double_array(w, response.response_r);
  w.key("g");
  write_double_array(w, response.response_g);
  w.key("b");
  write_double_array(w, response.response_b);
  w.end_object();
  w.key("line_spd");
  write_double_array(w, response.line_spd);
  w.key("normalization");
  w.value(response.normalization);
  w.key("validation_tier");
  w.value(response.validation_tier);
  w.end_object();
}

}  // namespace

void write_spectral_response_json(std::ostream& os,
                                  const SpectralResponse& response) {
  JsonWriter w(os);
  write_response_object(w, response);
}

void write_spectral_raw_extraction_json(
    std::ostream& os, const SpectralResponse& legacy,
    const SpectralRawExtraction& extraction) {
  JsonWriter w(os);
  w.begin_object();
  w.key("validation_tier");
  w.value(kLegacyValidationTier);
  w.key("legacy_response");
  write_response_object(w, legacy);
  w.key("toolkit_raw_extraction");
  write_response_object(w, extraction.response);
  w.key("extraction");
  w.begin_object();
  w.key("measurement_roi");
  write_roi(w, extraction.measurement_roi);
  w.key("near_saturation_fraction");
  w.value(extraction.near_saturation_fraction);
  w.key("metadata_black_by_position");
  write_position_array(w, extraction.metadata_black_by_position);
  w.key("dark_residual_mean_by_position");
  write_position_array(w, extraction.dark_residual_mean_by_position);
  w.key("samples");
  w.begin_array();
  for (const auto& sample : extraction.samples) {
    w.begin_object();
    w.key("wavelength_nm");
    w.value(sample.wavelength_nm);
    w.key("raw_path");
    const std::string raw_label =
        sample.raw_path.empty() ? "" : sample.raw_path.filename().generic_string();
    w.value(raw_label);
    w.key("mean_signal_rgb");
    w.begin_object();
    w.key("r");
    w.value(sample.mean_signal_r);
    w.key("g");
    w.value(sample.mean_signal_g);
    w.key("b");
    w.value(sample.mean_signal_b);
    w.end_object();
    w.key("saturated_fraction_rgb");
    w.begin_object();
    w.key("r");
    w.value(sample.saturated_fraction_r);
    w.key("g");
    w.value(sample.saturated_fraction_g);
    w.key("b");
    w.value(sample.saturated_fraction_b);
    w.end_object();
    w.key("below_dark_fraction_rgb");
    w.begin_object();
    w.key("r");
    w.value(sample.below_dark_fraction_r);
    w.key("g");
    w.value(sample.below_dark_fraction_g);
    w.key("b");
    w.value(sample.below_dark_fraction_b);
    w.end_object();
    w.end_object();
  }
  w.end_array();
  w.end_object();
  w.key("tier1_legacy_fidelity");
  w.begin_object();
  w.key("validation_tier");
  w.value(extraction.tier1_legacy_fidelity.validation_tier);
  w.key("r");
  write_fidelity_channel(w, extraction.tier1_legacy_fidelity.r);
  w.key("g");
  write_fidelity_channel(w, extraction.tier1_legacy_fidelity.g);
  w.key("b");
  write_fidelity_channel(w, extraction.tier1_legacy_fidelity.b);
  w.end_object();
  w.end_object();
}

}  // namespace camera_iq
