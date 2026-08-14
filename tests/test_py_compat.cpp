// Permanent test for py_compat.hpp. The expectations are the MEASURED Python
// values, not this port's behaviour, so this file should not change as the port
// grows. It is a separate ctest target for that reason.
#include "umi_tools/py_compat.hpp"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace {

int failures = 0;

void expect(const char* what, long long got, long long want) {
  if (got != want) {
    std::fprintf(stderr, "FAIL %s: got %lld, Python gives %lld\n",
                 what, got, want);
    ++failures;
  }
}

}  // namespace

int main() {
  using umi_tools::py_divmod;
  using umi_tools::py_floordiv;
  using umi_tools::py_mod;

  // Measured: python -c "print(-7//2, -7%2, 7//-2, 7%-2)" -> -4 1 -4 -1
  expect("-7 // 2",  py_floordiv(-7, 2),  -4);
  expect("-7 % 2",   py_mod(-7, 2),        1);
  expect("7 // -2",  py_floordiv(7, -2),  -4);
  expect("7 % -2",   py_mod(7, -2),       -1);

  // Same-sign operands: identical to the bare operators.
  expect("7 // 2",   py_floordiv(7, 2),    3);
  expect("7 % 2",    py_mod(7, 2),         1);
  expect("-7 // -2", py_floordiv(-7, -2),  3);
  expect("-7 % -2",  py_mod(-7, -2),      -1);

  // Exact division leaves nothing to fix up in either direction.
  expect("-8 // 2",  py_floordiv(-8, 2),  -4);
  expect("-8 % 2",   py_mod(-8, 2),        0);

  // Zero dividend, and the divisor-of-one edges.
  expect("0 // -3",  py_floordiv(0, -3),   0);
  expect("0 % -3",   py_mod(0, -3),        0);
  expect("-7 // -1", py_floordiv(-7, -1),  7);
  expect("-7 % -1",  py_mod(-7, -1),       0);

  // Wide types: the same fixup, and the width a bio index actually needs.
  expect("int64 -7 // 2", py_floordiv<std::int64_t>(-7, 2), -4);
  expect("int64 large",
         py_floordiv<std::int64_t>(-3000000000LL, 7), -428571429LL);

  auto dm = py_divmod(-7, 2);
  expect("divmod(-7, 2).quot", dm.quot, -4);
  expect("divmod(-7, 2).rem",  dm.rem,   1);

  // Division by zero raises in Python; it must not be UB here.
  try {
    (void)py_floordiv(1, 0);
    std::fprintf(stderr, "FAIL py_floordiv(1, 0) did not throw\n");
    ++failures;
  } catch (const std::domain_error&) {}
  try {
    (void)py_mod(1, 0);
    std::fprintf(stderr, "FAIL py_mod(1, 0) did not throw\n");
    ++failures;
  } catch (const std::domain_error&) {}

  // The one case fixed-width cannot reproduce: Python's answer is 2**63.
  try {
    (void)py_floordiv(std::numeric_limits<std::int64_t>::min(),
                      static_cast<std::int64_t>(-1));
    std::fprintf(stderr, "FAIL MIN / -1 did not throw\n");
    ++failures;
  } catch (const std::overflow_error&) {}

  if (failures == 0) std::printf("py_compat: all cases match Python\n");
  return failures == 0 ? 0 : 1;
}
