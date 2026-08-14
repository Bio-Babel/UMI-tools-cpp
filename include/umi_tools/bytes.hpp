// Bytes — a BYTE buffer, not text.
//
// UMIs and cell barcodes are Python `bytes` end to end (04_native_code.md): the
// compiled `edit_distance` kernel takes `cdef char *` and rejects `str` with
// `TypeError: expected bytes, str found`. Representing them as std::string is
// therefore not a convenience, it is the correct model, and it makes `len()` and
// `.size()` the same number — the code-point-versus-byte trap in the porting
// principles is discharged by construction rather than by luck.
//
// Comparison and ordering: Python compares and sorts `bytes` by unsigned byte
// value. std::string's operator< uses char_traits<char>::compare, which is
// memcmp, i.e. also UNSIGNED byte order — so `sorted()` on Python bytes and
// std::sort on Bytes agree even for bytes >= 0x80. That is worth stating because
// a naive `char`-by-`char` comparison (signed on x86) would NOT agree, and DNA
// data would never reveal the difference.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace umi_tools {

/// A byte string. Never interpreted as UTF-8, never case-folded.
using Bytes = std::string;
using BytesView = std::string_view;

/// Python's len(bytes). Provided so ported code reads like the original and so
/// there is one place to look when auditing length semantics.
inline std::int64_t py_len(BytesView b) noexcept {
  return static_cast<std::int64_t>(b.size());
}

/// Python's b[i], including negative indices (b[-1] is the last byte).
/// Python raises IndexError out of range; std::string::at throws
/// std::out_of_range, which is the mapping recorded in 06_design.md.
inline unsigned char py_at(BytesView b, std::int64_t i) {
  const std::int64_t n = py_len(b);
  if (i < 0) i += n;
  if (i < 0 || i >= n) throw std::out_of_range("bytes index out of range");
  return static_cast<unsigned char>(b[static_cast<std::size_t>(i)]);
}

/// Python's b[start:stop] with negative indices and clamping, which differs from
/// std::string::substr in every edge case: Python clamps silently where substr
/// throws, and Python accepts stop < start (yielding an empty result).
inline Bytes py_slice(BytesView b, std::int64_t start, std::int64_t stop) {
  const std::int64_t n = py_len(b);
  if (start < 0) start += n;
  if (stop < 0) stop += n;
  if (start < 0) start = 0;
  if (stop > n) stop = n;
  if (start >= stop) return Bytes{};
  return Bytes{b.substr(static_cast<std::size_t>(start),
                        static_cast<std::size_t>(stop - start))};
}

}  // namespace umi_tools
