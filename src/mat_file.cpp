#include "camera_iq/mat_file.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include <zlib.h>

namespace camera_iq {

namespace {

constexpr std::size_t kHeaderBytes = 128;

// MAT data types.
constexpr std::uint32_t kMiInt8 = 1;
constexpr std::uint32_t kMiUint8 = 2;
constexpr std::uint32_t kMiInt16 = 3;
constexpr std::uint32_t kMiUint16 = 4;
constexpr std::uint32_t kMiInt32 = 5;
constexpr std::uint32_t kMiUint32 = 6;
constexpr std::uint32_t kMiSingle = 7;
constexpr std::uint32_t kMiDouble = 9;
constexpr std::uint32_t kMiInt64 = 12;
constexpr std::uint32_t kMiUint64 = 13;
constexpr std::uint32_t kMiMatrix = 14;
constexpr std::uint32_t kMiCompressed = 15;

// MAT array classes.
constexpr std::uint32_t kMxStruct = 2;
constexpr std::uint32_t kMxDouble = 6;
constexpr std::uint32_t kMxSingle = 7;
constexpr std::uint32_t kMxInt8 = 8;
constexpr std::uint32_t kMxUint8 = 9;
constexpr std::uint32_t kMxInt16 = 10;
constexpr std::uint32_t kMxUint16 = 11;
constexpr std::uint32_t kMxInt32 = 12;
constexpr std::uint32_t kMxUint32 = 13;
constexpr std::uint32_t kMxInt64 = 14;
constexpr std::uint32_t kMxUint64 = 15;

constexpr std::uint32_t kArrayFlagLogical = 0x0200u;
constexpr std::uint32_t kArrayFlagComplex = 0x0800u;
constexpr int kMaxCompressedDepth = 4;
constexpr int kMaxMatrixDepth = 16;

// Inflates a deflate stream of unknown output size. MAT-files do not record the
// uncompressed length anywhere, so the buffer grows until zlib reports the end
// of the stream.
std::string inflate_stream(std::string_view in, std::size_t max_bytes) {
  if (in.size() > std::numeric_limits<uInt>::max()) {
    throw std::runtime_error("mat file: compressed element is too large for zlib");
  }
  z_stream zs{};
  if (inflateInit(&zs) != Z_OK) {
    throw std::runtime_error("mat file: could not start zlib inflate");
  }
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
  zs.avail_in = static_cast<uInt>(in.size());

  std::string out;
  char chunk[16384];
  int rc = Z_OK;
  do {
    zs.next_out = reinterpret_cast<Bytef*>(chunk);
    zs.avail_out = sizeof(chunk);
    rc = inflate(&zs, Z_NO_FLUSH);
    if (rc != Z_OK && rc != Z_STREAM_END) {
      inflateEnd(&zs);
      throw std::runtime_error("mat file: zlib inflate failed");
    }
    const std::size_t produced = sizeof(chunk) - zs.avail_out;
    if (out.size() > max_bytes || produced > max_bytes - out.size()) {
      inflateEnd(&zs);
      throw std::runtime_error(
          "mat file: compressed element inflates past the declared limit");
    }
    out.append(chunk, produced);
  } while (rc != Z_STREAM_END);
  if (zs.avail_in != 0) {
    inflateEnd(&zs);
    throw std::runtime_error("mat file: compressed element has trailing bytes");
  }
  inflateEnd(&zs);
  return out;
}

// One parsed data element: its type, its payload, and how many bytes of the
// stream it consumed including tag and padding.
struct Element {
  std::uint32_t type = 0;
  std::string_view payload;
  std::size_t consumed = 0;
};

std::uint32_t read_u32(std::string_view s, std::size_t off) {
  if (off > s.size() || s.size() - off < 4) {
    throw std::runtime_error("mat file: truncated 32-bit value");
  }
  const auto* p = reinterpret_cast<const unsigned char*>(s.data() + off);
  return static_cast<std::uint32_t>(p[0]) |
         (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) |
         (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_u64(std::string_view s, std::size_t off) {
  if (off > s.size() || s.size() - off < 8) {
    throw std::runtime_error("mat file: truncated 64-bit value");
  }
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(
                 static_cast<unsigned char>(s[off + static_cast<std::size_t>(i)]))
             << (8 * i);
  }
  return value;
}

// MAT-files use a compact tag when a payload is at most four bytes: the byte
// count moves into the high half of the first word. Real files rely on it for
// short fields such as a struct's field-name length, so a reader that only
// understands the long form fails on the first genuine file it sees.
Element read_element(std::string_view s, std::size_t off) {
  if (off > s.size() || s.size() - off < 8) {
    throw std::runtime_error("mat file: truncated element tag");
  }
  const std::uint32_t w0 = read_u32(s, off);
  Element e;
  if ((w0 >> 16) != 0) {
    e.type = w0 & 0xFFFFu;
    const std::size_t n = w0 >> 16;
    if (n > 4) throw std::runtime_error("mat file: bad compact element size");
    e.payload = s.substr(off + 4, n);
    e.consumed = 8;
    return e;
  }
  e.type = w0;
  const std::size_t n = read_u32(s, off + 4);
  const std::size_t available = s.size() - off - 8;
  // The Level-5 format aligns uncompressed data fields to 64-bit boundaries.
  // miCOMPRESSED is the explicit exception: its zlib stream is followed
  // immediately by the next tag or end of file. The archive's 134 measurement
  // files do not establish this -- each holds one top-level element, so the
  // walk ends at the buffer under either rule. Its 16 legacy workspace saves
  // do: they chain 35 to 45 compressed elements, and only the unpadded rule
  // reaches a clean end with readable variable names.
  const std::size_t padding =
      e.type == kMiCompressed ? 0 : (8 - n % 8) % 8;
  if (n > available || padding > available - n) {
    throw std::runtime_error("mat file: element runs past the end of the data");
  }
  e.payload = s.substr(off + 8, n);
  e.consumed = 8 + n + padding;
  return e;
}

// Widens any MAT numeric payload to double. The archive stores one struct with
// mixed widths -- radiance as double, the wavelength axis as uint16, the repeat
// counters as uint8 -- so the caller gets one representation rather than having
// to branch on how each field happened to be written.
std::size_t element_width(std::uint32_t type) {
  switch (type) {
    case kMiInt8: case kMiUint8: return 1;
    case kMiInt16: case kMiUint16: return 2;
    case kMiInt32: case kMiUint32: case kMiSingle: return 4;
    case kMiDouble: case kMiInt64: case kMiUint64: return 8;
    default: return 0;
  }
}

std::vector<double> numeric_values(const Element& e) {
  const std::size_t width = element_width(e.type);
  if (width == 0) {
    throw std::runtime_error("mat file: unsupported numeric element type");
  }
  // A payload that is not a whole number of samples means the element was
  // misread. Rounding down would drop a sample and still return a plausible
  // array, which is worse than refusing.
  if (e.payload.size() % width != 0) {
    throw std::runtime_error(
        "mat file: numeric payload is not a whole number of samples");
  }
  const std::size_t count = e.payload.size() / width;
  std::vector<double> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t off = i * width;
    switch (e.type) {
      case kMiInt8:
        out.push_back(static_cast<double>(
            std::bit_cast<std::int8_t>(
                static_cast<std::uint8_t>(e.payload[off]))));
        break;
      case kMiUint8:
        out.push_back(static_cast<double>(
            static_cast<unsigned char>(e.payload[off])));
        break;
      case kMiInt16: {
        const std::uint16_t bits = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                static_cast<unsigned char>(e.payload[off])) |
            (static_cast<std::uint16_t>(
                 static_cast<unsigned char>(e.payload[off + 1]))
             << 8));
        out.push_back(static_cast<double>(std::bit_cast<std::int16_t>(bits)));
        break;
      }
      case kMiUint16: {
        const std::uint16_t value = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(
                static_cast<unsigned char>(e.payload[off])) |
            (static_cast<std::uint16_t>(
                 static_cast<unsigned char>(e.payload[off + 1]))
             << 8));
        out.push_back(static_cast<double>(value));
        break;
      }
      case kMiInt32:
        out.push_back(static_cast<double>(
            std::bit_cast<std::int32_t>(read_u32(e.payload, off))));
        break;
      case kMiUint32:
        out.push_back(static_cast<double>(read_u32(e.payload, off)));
        break;
      case kMiSingle:
        out.push_back(static_cast<double>(
            std::bit_cast<float>(read_u32(e.payload, off))));
        break;
      case kMiDouble:
        out.push_back(std::bit_cast<double>(read_u64(e.payload, off)));
        break;
      case kMiInt64: {
        const std::int64_t value =
            std::bit_cast<std::int64_t>(read_u64(e.payload, off));
        const std::uint64_t magnitude =
            value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                      : static_cast<std::uint64_t>(value);
        const int significant_bits =
            magnitude == 0 ? 0 : 64 - std::countl_zero(magnitude);
        const int discarded_bits = std::max(0, significant_bits - 53);
        const std::uint64_t discarded_mask =
            discarded_bits == 0
                ? 0
                : (std::uint64_t{1} << discarded_bits) - 1;
        if ((magnitude & discarded_mask) != 0) {
          throw std::runtime_error(
              "mat file: int64 value cannot be represented exactly as double");
        }
        out.push_back(static_cast<double>(value));
        break;
      }
      case kMiUint64: {
        const std::uint64_t value = read_u64(e.payload, off);
        const int significant_bits =
            value == 0 ? 0 : 64 - std::countl_zero(value);
        const int discarded_bits = std::max(0, significant_bits - 53);
        const std::uint64_t discarded_mask =
            discarded_bits == 0
                ? 0
                : (std::uint64_t{1} << discarded_bits) - 1;
        if ((value & discarded_mask) != 0) {
          throw std::runtime_error(
              "mat file: uint64 value cannot be represented exactly as double");
        }
        out.push_back(static_cast<double>(value));
        break;
      }
      default:
        throw std::runtime_error("mat file: unsupported numeric element type");
    }
  }
  return out;
}

struct Matrix {
  std::uint32_t klass = 0;
  std::uint32_t flags = 0;
  bool logical = false;
  // The array's own name at top level; the field name when nested in a struct.
  std::string name;
  std::vector<std::size_t> dims;
  std::vector<double> values;  // numeric arrays
  // Field names are stored in each child so recursive storage does not require
  // a pair instantiated on an incomplete Matrix type.
  std::vector<Matrix> fields;  // struct arrays
};

struct MatrixHeader {
  std::uint32_t klass = 0;
  std::uint32_t flags = 0;
  std::vector<std::size_t> dims;
  std::string name;
  std::size_t content_offset = 0;
};

std::size_t checked_element_count(const std::vector<std::size_t>& dims) {
  if (dims.empty()) return 0;
  std::size_t count = 1;
  for (const std::size_t dim : dims) {
    if (dim != 0 && count > std::numeric_limits<std::size_t>::max() / dim) {
      throw std::runtime_error("mat file: array dimensions overflow");
    }
    count *= dim;
  }
  return count;
}

MatrixHeader parse_matrix_header(std::string_view body) {
  MatrixHeader header;
  std::size_t off = 0;

  const Element flags = read_element(body, off);
  off += flags.consumed;
  if (flags.type != kMiUint32 || flags.payload.size() != 8) {
    throw std::runtime_error("mat file: array flags are malformed");
  }
  const std::uint32_t flag_word = read_u32(flags.payload, 0);
  header.klass = flag_word & 0xFFu;
  header.flags = flag_word & ~0xFFu;

  const Element dims = read_element(body, off);
  off += dims.consumed;
  if (dims.type != kMiInt32 || dims.payload.empty()) {
    throw std::runtime_error("mat file: dimensions must be signed 32-bit integers");
  }
  for (const double value : numeric_values(dims)) {
    if (!std::isfinite(value) || value < 0.0 ||
        value > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
      throw std::runtime_error("mat file: array dimension is invalid");
    }
    header.dims.push_back(static_cast<std::size_t>(value));
  }
  (void)checked_element_count(header.dims);

  const Element name = read_element(body, off);
  off += name.consumed;
  if (name.type != kMiInt8 && name.type != kMiUint8) {
    throw std::runtime_error("mat file: array name is not byte text");
  }
  header.name.assign(name.payload);
  if (header.name.find('\0') != std::string::npos) {
    throw std::runtime_error("mat file: array name contains an embedded NUL");
  }
  header.content_offset = off;
  return header;
}

bool supported_numeric_class(std::uint32_t klass) {
  switch (klass) {
    case kMxDouble:
    case kMxSingle:
    case kMxInt8:
    case kMxUint8:
    case kMxInt16:
    case kMxUint16:
    case kMxInt32:
    case kMxUint32:
    case kMxInt64:
    case kMxUint64:
      return true;
    default:
      return false;
  }
}

Matrix parse_matrix(std::string_view body, int depth);

// A struct array's body continues, after the common header, with the field-name
// length, the packed NUL-padded names, and then one nested matrix per field.
void parse_struct_fields(std::string_view body, std::size_t off, Matrix& m,
                         int depth) {
  const Element len_el = read_element(body, off);
  off += len_el.consumed;
  if (len_el.type != kMiInt32) {
    throw std::runtime_error(
        "mat file: struct field-name length must be signed 32-bit");
  }
  const std::vector<double> lens = numeric_values(len_el);
  if (lens.size() != 1 || !std::isfinite(lens[0]) || lens[0] <= 0 ||
      lens[0] > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
    throw std::runtime_error("mat file: struct field-name length is not set");
  }
  const std::size_t name_len = static_cast<std::size_t>(lens[0]);

  const Element names_el = read_element(body, off);
  off += names_el.consumed;
  if (names_el.type != kMiInt8 || names_el.payload.size() % name_len != 0) {
    throw std::runtime_error("mat file: malformed struct field-name table");
  }
  const std::size_t count = names_el.payload.size() / name_len;

  std::vector<std::string> names;
  std::set<std::string> unique_names;
  names.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::string_view raw = names_el.payload.substr(i * name_len, name_len);
    const std::size_t end = raw.find('\0');
    if (end == std::string_view::npos || end == 0 ||
        raw.substr(end).find_first_not_of('\0') != std::string_view::npos) {
      throw std::runtime_error("mat file: malformed struct field name");
    }
    std::string field_name(raw.substr(0, end));
    if (!unique_names.insert(field_name).second) {
      throw std::runtime_error("mat file: duplicate struct field name");
    }
    names.push_back(std::move(field_name));
  }

  for (std::size_t i = 0; i < count; ++i) {
    const Element field_el = read_element(body, off);
    off += field_el.consumed;
    if (field_el.type != kMiMatrix) {
      throw std::runtime_error("mat file: struct field is not an array");
    }
    Matrix field = parse_matrix(field_el.payload, depth + 1);
    if (!field.name.empty()) {
      throw std::runtime_error("mat file: struct field array has a non-empty name");
    }
    field.name = names[i];
    m.fields.push_back(std::move(field));
  }
  if (off != body.size()) {
    throw std::runtime_error("mat file: struct contains unexpected trailing data");
  }
}

Matrix parse_matrix(std::string_view body, int depth) {
  if (depth > kMaxMatrixDepth) {
    throw std::runtime_error("mat file: matrices nested too deeply");
  }
  const MatrixHeader header = parse_matrix_header(body);
  Matrix m;
  m.klass = header.klass;
  m.flags = header.flags;
  m.logical = (m.flags & kArrayFlagLogical) != 0;
  m.dims = header.dims;
  m.name = header.name;

  if ((m.flags & kArrayFlagComplex) != 0) {
    throw std::runtime_error("mat file: complex arrays are unsupported");
  }

  if (m.klass == kMxStruct) {
    if (checked_element_count(m.dims) != 1) {
      throw std::runtime_error("mat file: only scalar structs are supported");
    }
    parse_struct_fields(body, header.content_offset, m, depth);
    return m;
  }

  if (!supported_numeric_class(m.klass)) {
    throw std::runtime_error("mat file: unsupported numeric array class");
  }
  const Element real = read_element(body, header.content_offset);
  if (element_width(real.type) == 0) {
    throw std::runtime_error("mat file: unsupported numeric element type");
  }
  if (header.content_offset + real.consumed != body.size()) {
    throw std::runtime_error("mat file: numeric array has unexpected trailing data");
  }
  m.values = numeric_values(real);
  if (m.logical) {
    if (m.klass != kMxUint8 || real.type != kMiUint8) {
      throw std::runtime_error(
          "mat file: logical arrays require uint8 class and storage");
    }
    for (const double value : m.values) {
      if (value != 0.0 && value != 1.0) {
        throw std::runtime_error("mat file: logical payload is not binary");
      }
    }
  }
  // Dimensions are what a caller indexes with, so they must describe the values
  // that are actually present.
  const std::size_t expected = checked_element_count(m.dims);
  if (expected != m.values.size()) {
    throw std::runtime_error(
        "mat file: array dimensions disagree with the value count");
  }
  return m;
}

struct StructSearch {
  std::optional<Matrix> match;
};

// Scans common matrix headers first and decodes only the named variable.
// Unsupported workspace variables are therefore irrelevant to archive ingest,
// while a malformed common header still refuses because scanning cannot safely
// locate the next element.
void find_struct(std::string_view data, std::string_view want,
                 StructSearch& search, std::size_t& remaining_bytes, int depth) {
  // MATLAB nests one compressed element per file. A deeper chain is either a
  // format this reader has not been shown or a crafted input.
  if (depth > kMaxCompressedDepth) {
    throw std::runtime_error("mat file: compressed elements nested too deeply");
  }
  std::size_t off = 0;
  while (off < data.size()) {
    const Element e = read_element(data, off);
    off += e.consumed;
    if (e.type == kMiCompressed) {
      const std::string inflated = inflate_stream(e.payload, remaining_bytes);
      remaining_bytes -= inflated.size();
      find_struct(inflated, want, search, remaining_bytes, depth + 1);
      continue;
    }
    if (e.type != kMiMatrix) continue;
    const MatrixHeader header = parse_matrix_header(e.payload);
    if (header.name != want) continue;
    if (header.klass != kMxStruct) {
      throw std::runtime_error("mat file: named variable is not a struct");
    }
    if (search.match) {
      throw std::runtime_error("mat file: duplicate named struct variable");
    }
    search.match = parse_matrix(e.payload, 0);
  }
}

}  // namespace

MatStruct read_mat_struct(const std::string& bytes,
                          std::string_view variable_name,
                          std::size_t max_inflated_bytes) {
  if (bytes.size() < kHeaderBytes) {
    throw std::runtime_error("mat file: shorter than the 128-byte v5 header");
  }
  // Bytes 126-127 carry the endian indicator. MATLAB writes "IM" on a
  // little-endian host; anything else is not a v5 MAT-file this reader accepts.
  if (!(bytes[kHeaderBytes - 2] == 'I' && bytes[kHeaderBytes - 1] == 'M')) {
    throw std::runtime_error(
        "mat file: missing the little-endian v5 indicator \"IM\"");
  }
  // Bytes 124-125 are the version. Every v5 file writes 0x0100; a different
  // value is a format this reader has not been shown to parse.
  const auto v_lo = static_cast<unsigned char>(bytes[kHeaderBytes - 4]);
  const auto v_hi = static_cast<unsigned char>(bytes[kHeaderBytes - 3]);
  if (!(v_lo == 0x00 && v_hi == 0x01)) {
    throw std::runtime_error("mat file: unsupported MAT version field");
  }

  const std::string_view data(bytes);
  if (variable_name.empty()) {
    throw std::runtime_error("mat file: requested variable name is empty");
  }
  StructSearch search;
  std::size_t remaining_inflated_bytes = max_inflated_bytes;
  find_struct(data.substr(kHeaderBytes), variable_name, search,
              remaining_inflated_bytes, 0);
  if (!search.match) {
    throw std::runtime_error("mat file: no struct variable named \"" +
                             std::string(variable_name) + "\"");
  }

  MatStruct out;
  for (const Matrix& field : search.match->fields) {
    if (field.klass == kMxStruct) {
      throw std::runtime_error("mat file: nested struct fields are unsupported");
    }
    const auto inserted =
        out.emplace(field.name,
                    MatArray{field.dims, field.values, field.logical});
    if (!inserted.second) {
      throw std::runtime_error("mat file: duplicate struct field name");
    }
  }
  return out;
}

}  // namespace camera_iq
