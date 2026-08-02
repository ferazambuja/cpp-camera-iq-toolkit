#include "camera_iq/mat_file.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
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

// Inflates a deflate stream of unknown output size. MAT-files do not record the
// uncompressed length anywhere, so the buffer grows until zlib reports the end
// of the stream.
std::string inflate_stream(std::string_view in) {
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
    out.append(chunk, sizeof(chunk) - zs.avail_out);
  } while (rc != Z_STREAM_END);
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
  std::uint32_t v = 0;
  std::memcpy(&v, s.data() + off, 4);
  return v;
}

// MAT-files use a compact tag when a payload is at most four bytes: the byte
// count moves into the high half of the first word. Real files rely on it for
// short fields such as a struct's field-name length, so a reader that only
// understands the long form fails on the first genuine file it sees.
Element read_element(std::string_view s, std::size_t off) {
  if (off + 8 > s.size()) {
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
  if (off + 8 + n > s.size()) {
    throw std::runtime_error("mat file: element runs past the end of the data");
  }
  e.payload = s.substr(off + 8, n);
  e.consumed = 8 + n + ((8 - n % 8) % 8);
  return e;
}

// Widens any MAT numeric payload to double. The archive stores one struct with
// mixed widths -- radiance as double, the wavelength axis as uint16, the repeat
// counters as uint8 -- so the caller gets one representation rather than having
// to branch on how each field happened to be written.
template <typename T>
void append_as_double(std::string_view p, std::vector<double>& out) {
  const std::size_t n = p.size() / sizeof(T);
  for (std::size_t i = 0; i < n; ++i) {
    T v{};
    std::memcpy(&v, p.data() + i * sizeof(T), sizeof(T));
    out.push_back(static_cast<double>(v));
  }
}

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
  std::vector<double> out;
  switch (e.type) {
    case kMiInt8: append_as_double<std::int8_t>(e.payload, out); break;
    case kMiUint8: append_as_double<std::uint8_t>(e.payload, out); break;
    case kMiInt16: append_as_double<std::int16_t>(e.payload, out); break;
    case kMiUint16: append_as_double<std::uint16_t>(e.payload, out); break;
    case kMiInt32: append_as_double<std::int32_t>(e.payload, out); break;
    case kMiUint32: append_as_double<std::uint32_t>(e.payload, out); break;
    case kMiSingle: append_as_double<float>(e.payload, out); break;
    case kMiDouble: append_as_double<double>(e.payload, out); break;
    case kMiInt64: append_as_double<std::int64_t>(e.payload, out); break;
    case kMiUint64: append_as_double<std::uint64_t>(e.payload, out); break;
    default:
      throw std::runtime_error("mat file: unsupported numeric element type");
  }
  return out;
}

struct Matrix {
  std::uint32_t klass = 0;
  // The array's own name at top level; the field name when nested in a struct.
  std::string name;
  std::vector<std::size_t> dims;
  std::vector<double> values;  // numeric arrays
  // vector, list and forward_list may be instantiated on an incomplete type
  // when used as a member of that type; other class templates may not. Holding
  // std::pair<std::string, Matrix> here instantiates pair on an incomplete
  // Matrix, which is ill-formed with no diagnostic required -- so a compiler
  // that accepts it is not wrong, and one that rejects it is not either. Both
  // Linux CI jobs use libstdc++: GCC compiled it and Clang did not.
  std::vector<Matrix> fields;  // struct arrays
};

Matrix parse_matrix(std::string_view body);

// A struct array's body continues, after the common header, with the field-name
// length, the packed NUL-padded names, and then one nested matrix per field.
void parse_struct_fields(std::string_view body, std::size_t off, Matrix& m) {
  const Element len_el = read_element(body, off);
  off += len_el.consumed;
  const std::vector<double> lens = numeric_values(len_el);
  if (lens.empty() || lens[0] <= 0) {
    throw std::runtime_error("mat file: struct field-name length is not set");
  }
  const std::size_t name_len = static_cast<std::size_t>(lens[0]);

  const Element names_el = read_element(body, off);
  off += names_el.consumed;
  const std::size_t count = names_el.payload.size() / name_len;

  std::vector<std::string> names;
  names.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const std::string_view raw = names_el.payload.substr(i * name_len, name_len);
    names.emplace_back(raw.substr(0, raw.find('\0')));
  }

  for (std::size_t i = 0; i < count; ++i) {
    const Element field_el = read_element(body, off);
    off += field_el.consumed;
    if (field_el.type != kMiMatrix) {
      throw std::runtime_error("mat file: struct field is not an array");
    }
    Matrix field = parse_matrix(field_el.payload);
    field.name = names[i];
    m.fields.push_back(std::move(field));
  }
}

Matrix parse_matrix(std::string_view body) {
  Matrix m;
  std::size_t off = 0;

  const Element flags = read_element(body, off);
  off += flags.consumed;
  if (flags.payload.size() < 8) {
    throw std::runtime_error("mat file: array flags are truncated");
  }
  m.klass = read_u32(flags.payload, 0) & 0xFFu;

  const Element dims = read_element(body, off);
  off += dims.consumed;
  for (const double d : numeric_values(dims)) {
    m.dims.push_back(static_cast<std::size_t>(d));
  }

  const Element name = read_element(body, off);
  off += name.consumed;
  m.name.assign(name.payload);

  if (m.klass == kMxStruct) {
    parse_struct_fields(body, off, m);
    return m;
  }

  const Element real = read_element(body, off);
  m.values = numeric_values(real);
  // Dimensions are what a caller indexes with, so they must describe the values
  // that are actually present.
  std::size_t expected = m.dims.empty() ? 0 : 1;
  for (const std::size_t d : m.dims) expected *= d;
  if (expected != m.values.size()) {
    throw std::runtime_error(
        "mat file: array dimensions disagree with the value count");
  }
  return m;
}

// Scans a MAT element stream for the first struct variable. A compressed
// element is inflated and scanned in turn: MATLAB wraps a file's whole payload
// in one, so the struct is always one level down in practice.
bool find_struct(std::string_view data, MatStruct& out) {
  std::size_t off = 0;
  while (off < data.size()) {
    const Element e = read_element(data, off);
    off += e.consumed;
    if (e.type == kMiCompressed) {
      const std::string inflated = inflate_stream(e.payload);
      if (find_struct(inflated, out)) return true;
      continue;
    }
    if (e.type != kMiMatrix) continue;
    const Matrix m = parse_matrix(e.payload);
    if (m.klass != kMxStruct) continue;
    for (const Matrix& field : m.fields) {
      out.emplace(field.name, MatArray{field.dims, field.values});
    }
    return true;
  }
  return false;
}

}  // namespace

MatStruct read_mat_struct(const std::string& bytes) {
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
  MatStruct out;
  if (find_struct(data.substr(kHeaderBytes), out)) return out;
  throw std::runtime_error("mat file: no struct variable found");
}

}  // namespace camera_iq
