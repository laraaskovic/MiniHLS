#pragma once

#include <cstdint>

// Fixed-width integer semantics shared by every stage of the compiler.
//
// The interpreters, the optimizer's range analysis, and the co-simulation
// testbenches all need to agree bit-for-bit on what a value of a given width
// means. Keeping that in one place is what makes the interpreters usable as
// trusted references for the generated RTL.
namespace minihls {

// Largest width these helpers model. Values wider than a machine word need a
// different representation, which the language deliberately does not admit yet.
inline constexpr unsigned kMaxWidth = 64;

// Bit mask with the low `width` bits set. `width` must be in [1, kMaxWidth].
std::uint64_t mask_for_width(unsigned width);

// Drop every bit above `width`, modelling assignment to a narrower type.
std::uint64_t truncate(std::uint64_t bits, unsigned width);

// Reinterpret the low `width` bits as two's complement and widen to 64 bits.
std::int64_t sign_extend(std::uint64_t bits, unsigned width);

// Reinterpret the low `width` bits as unsigned and widen to 64 bits.
std::uint64_t zero_extend(std::uint64_t bits, unsigned width);

// Smallest and largest value representable in `width` bits at the given
// signedness. Range analysis seeds its lattice from these.
std::int64_t min_value(unsigned width, bool is_signed);
std::int64_t max_value(unsigned width, bool is_signed);

// Width rules from LANGUAGE.md. Arithmetic grows so that the operation itself
// can never overflow; only an explicit assignment narrows a value back down.
//
// Addition takes the wider operand plus one bit for the carry.
unsigned add_result_width(unsigned lhs_width, unsigned rhs_width);

// Multiplication takes the sum of the operand widths.
unsigned mul_result_width(unsigned lhs_width, unsigned rhs_width);

// Minimum number of bits needed to hold `value` at the given signedness. Used
// to report how much bit-width analysis actually narrowed a program.
unsigned bits_needed(std::int64_t value, bool is_signed);

}  // namespace minihls
