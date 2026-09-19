#pragma once

#include "support/bits.hpp"

#include <cstdint>
#include <string>

// Exact integer arithmetic for evaluating the language's width rules.
//
// Every value the language admits fits in 64 bits, but the operand of an
// operation that grows -- `u64` widened by mixed signedness, the full product of
// two 32-bit values, the sum of two `i64`s before truncation -- does not fit in
// a machine word of either signedness. A 128-bit intermediate holds all of them
// exactly, which lets the interpreters compute the mathematical result first and
// then apply the width rule, rather than reasoning about overflow case by case.
namespace minihls {

__extension__ typedef __int128 Int128;
__extension__ typedef unsigned __int128 UInt128;

// The low `width` bits of `bits` read as a mathematical integer.
inline Int128 to_int128(std::uint64_t bits, unsigned width, bool is_signed) {
  if (is_signed) return static_cast<Int128>(sign_extend(bits, width));
  return static_cast<Int128>(zero_extend(bits, width));
}

// The low `width` bits of a two's complement value. This is assignment to a
// narrower type, and it is also exact whenever the value already fits.
inline std::uint64_t from_int128(Int128 value, unsigned width) {
  return truncate(static_cast<std::uint64_t>(value), width);
}

// Decimal rendering; the standard library has no overload for a 128-bit value.
inline std::string int128_to_string(Int128 value) {
  if (value == 0) return "0";
  const bool negative = value < 0;
  // Negate through the unsigned type so the most negative value is safe.
  UInt128 magnitude = negative ? -static_cast<UInt128>(value) : static_cast<UInt128>(value);
  std::string digits;
  while (magnitude != 0) {
    digits.insert(digits.begin(), static_cast<char>('0' + static_cast<int>(magnitude % 10)));
    magnitude /= 10;
  }
  if (negative) digits.insert(digits.begin(), '-');
  return digits;
}

// Renders `bits` as a signed or unsigned decimal of the given width.
inline std::string value_to_string(std::uint64_t bits, unsigned width, bool is_signed) {
  return int128_to_string(to_int128(bits, width, is_signed));
}

}  // namespace minihls
