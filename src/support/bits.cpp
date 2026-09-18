#include "support/bits.hpp"

#include <bit>
#include <cassert>
#include <limits>

namespace minihls {
namespace {

// Number of bits required to represent `value` as a plain magnitude, where zero
// needs none. Kept private because callers almost always want bits_needed(),
// which accounts for a sign bit.
unsigned significant_bits(std::uint64_t value) {
  return value == 0 ? 0u : kMaxWidth - static_cast<unsigned>(std::countl_zero(value));
}

}  // namespace

std::uint64_t mask_for_width(unsigned width) {
  assert(width >= 1 && width <= kMaxWidth);
  // Shifting by the full word width is undefined, so the widest mask is built
  // by complement instead.
  if (width >= kMaxWidth) return ~std::uint64_t{0};
  return (std::uint64_t{1} << width) - 1;
}

std::uint64_t truncate(std::uint64_t bits, unsigned width) {
  return bits & mask_for_width(width);
}

std::int64_t sign_extend(std::uint64_t bits, unsigned width) {
  const std::uint64_t mask = mask_for_width(width);
  bits &= mask;
  const std::uint64_t sign_bit = std::uint64_t{1} << (width - 1);
  if (bits & sign_bit) bits |= ~mask;
  return static_cast<std::int64_t>(bits);
}

std::uint64_t zero_extend(std::uint64_t bits, unsigned width) {
  return truncate(bits, width);
}

std::int64_t min_value(unsigned width, bool is_signed) {
  assert(width >= 1 && width <= kMaxWidth);
  if (!is_signed) return 0;
  if (width >= kMaxWidth) return std::numeric_limits<std::int64_t>::min();
  return -(std::int64_t{1} << (width - 1));
}

std::int64_t max_value(unsigned width, bool is_signed) {
  assert(width >= 1 && width <= kMaxWidth);
  if (is_signed) {
    if (width >= kMaxWidth) return std::numeric_limits<std::int64_t>::max();
    return (std::int64_t{1} << (width - 1)) - 1;
  }
  // A full 64-bit unsigned maximum does not fit the signed return type. The
  // language's declared widths stay below that, and sema will reject the rest.
  assert(width < kMaxWidth);
  return static_cast<std::int64_t>(mask_for_width(width));
}

unsigned add_result_width(unsigned lhs_width, unsigned rhs_width) {
  assert(lhs_width >= 1 && rhs_width >= 1);
  const unsigned wider = lhs_width > rhs_width ? lhs_width : rhs_width;
  assert(wider < kMaxWidth && "addition would exceed the modelled width");
  return wider + 1;
}

unsigned mul_result_width(unsigned lhs_width, unsigned rhs_width) {
  assert(lhs_width >= 1 && rhs_width >= 1);
  assert(lhs_width + rhs_width <= kMaxWidth &&
         "multiplication would exceed the modelled width");
  return lhs_width + rhs_width;
}

unsigned bits_needed(std::int64_t value, bool is_signed) {
  if (!is_signed) {
    assert(value >= 0 && "an unsigned value cannot be negative");
    const unsigned bits = significant_bits(static_cast<std::uint64_t>(value));
    return bits == 0 ? 1u : bits;
  }
  if (value < 0) {
    // The negative range reaches one further than the positive one, so measure
    // -(value + 1) to keep the arithmetic inside the signed domain.
    return significant_bits(static_cast<std::uint64_t>(-(value + 1))) + 1;
  }
  return significant_bits(static_cast<std::uint64_t>(value)) + 1;
}

}  // namespace minihls
