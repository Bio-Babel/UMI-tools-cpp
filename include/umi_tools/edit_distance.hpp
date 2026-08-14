// edit_distance — the port of umi_tools/_dedup_umi.pyx's single export.
//
// Full contract, measured against the live oracle, in 04_native_code.md. The
// short version, because both halves matter:
//
//   * For EQUAL-length inputs it is a Hamming distance: the count of positions
//     at which the two byte strings differ. Not Levenshtein — no DP table, no
//     insertions or deletions. The .pyx comment says so and the loop confirms.
//
//   * For UNEQUAL-length inputs the oracle RAISES. The .pyx says
//     `return np.inf` from a function declared `cpdef int`, but the shipped
//     _dedup_umi.c (which is what is actually compiled and installed) looks up
//     `np.Inf`, removed in numpy 2.0, so with the oracle's numpy 2.4.6 it raises
//     AttributeError. Neither source has a working unequal-length path: had the
//     .c matched the .pyx, Cython's float->int conversion would have raised
//     OverflowError instead. There is no upstream behaviour here to preserve
//     beyond "it raises", so this throws.
//
//     This path is unreachable on every live call site — UMIClusterer asserts
//     all UMIs are equal length before calling, and the one caller that could
//     pass unequal lengths (CellClusterer) cannot execute at all. That is why
//     the bug survived upstream.
//
// Porting it from the .pyx text alone would have produced a function returning a
// huge sentinel, which compares as "very far apart" and silently changes
// clustering. Hence: read AND run.
#pragma once

#include <stdexcept>
#include <string_view>

namespace umi_tools {

/// Hamming distance between two equal-length byte strings.
/// Throws std::invalid_argument when the lengths differ (see above).
inline int edit_distance(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    // Mirrors the oracle's failure, deliberately. Deviation-ledger entry in
    // 10_validation.md: the oracle raises AttributeError from numpy, which has
    // no C++ counterpart; the observable behaviour (an error, not a number) is
    // what is reproduced.
    throw std::invalid_argument(
        "edit_distance: barcodes of unequal length (the Python oracle raises "
        "AttributeError here: `np.Inf` was removed in the NumPy 2.0 release)");
  }
  int c = 0;
  for (std::size_t k = 0; k < a.size(); ++k) {
    // Compared as raw bytes. Signedness of `char` is irrelevant to inequality,
    // but the cast keeps the intent explicit: these are bytes, not characters.
    if (static_cast<unsigned char>(a[k]) != static_cast<unsigned char>(b[k])) ++c;
  }
  return c;
}

}  // namespace umi_tools
