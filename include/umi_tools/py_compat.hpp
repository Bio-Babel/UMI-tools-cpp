// Python integer semantics, for a port that must reproduce them.
//
// C++ `/` truncates toward zero and `%` takes the sign of the dividend; Python
// `//` floors and `%` takes the sign of the DIVISOR. They agree only when both
// operands have the same sign, so every `/` or `%` in ported code whose operands
// can be negative belongs here instead. Measured on this toolchain:
//
//     expression   Python    bare C++
//     -7 // 2        -4        -3
//     -7 %  2         1        -1
//      7 // -2       -4        -3
//      7 %  -2       -1         1
//
// Coordinates, offsets, and anything derived from a subtraction can be negative
// even when the inputs are not. If you can PROVE non-negativity, use the bare
// operator and say why in the translation log; otherwise use these.
#pragma once

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace umi_tools {

/// Python raises ZeroDivisionError; bare C++ integer division by zero is UB, so
/// these throw rather than letting the optimiser act on an impossible state.
/// pybind11 maps std::domain_error to ValueError — register a ZeroDivisionError
/// translation in the bindings if a caller catches that specific type.
template <typename T>
constexpr T py_floordiv(T a, T b) {
  static_assert(std::is_integral_v<T> && std::is_signed_v<T>,
                "py_floordiv is for signed integers; float division already "
                "matches, and unsigned operands cannot hit the sign case");
  if (b == 0) throw std::domain_error("py_floordiv: integer division by zero");
  // The true quotient of MIN / -1 is not representable; Python has no such
  // limit, so this case cannot be reproduced rather than merely differing.
  if (b == -1 && a == std::numeric_limits<T>::min())
    throw std::overflow_error("py_floordiv: quotient not representable");
  T q = a / b;
  T r = a % b;
  // Truncation and flooring differ by one exactly when there is a remainder and
  // the operands have opposite signs.
  if (r != 0 && ((r < 0) != (b < 0))) --q;
  return q;
}

template <typename T>
constexpr T py_mod(T a, T b) {
  static_assert(std::is_integral_v<T> && std::is_signed_v<T>,
                "py_mod is for signed integers");
  if (b == 0) throw std::domain_error("py_mod: integer modulo by zero");
  if (b == -1) return 0;   // avoids the MIN % -1 overflow; the answer is 0
  T r = a % b;
  if (r != 0 && ((r < 0) != (b < 0))) r += b;
  return r;
}

/// Python's divmod: both results in one pass. Prefer this over calling both
/// helpers when a loop needs the quotient and the remainder.
template <typename T>
struct DivMod {
  T quot;
  T rem;
};

template <typename T>
constexpr DivMod<T> py_divmod(T a, T b) {
  return {py_floordiv(a, b), py_mod(a, b)};
}

}  // namespace umi_tools
