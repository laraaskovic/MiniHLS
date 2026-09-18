// Co-simulation of the hand-written adder against the C++ reference semantics.
//
// This is the pattern every later milestone reuses: drive a Verilated model and
// an independent software model with the same stimulus and require bit-exact
// agreement. Here the software model is plain 64-bit arithmetic, because the
// whole point of the width rule is that the sum cannot overflow.

#include "Vadder16.h"
#include "Vadder8.h"
#include "support/bits.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <random>

namespace {

// Verilator exposes each top-level port as a reference into the model's own
// storage, so the port type is wider than the signal it stands for and the
// operand width has to be enforced here.
template <typename Port>
void drive(Port& port, std::int64_t value, unsigned width) {
  port = static_cast<Port>(minihls::truncate(static_cast<std::uint64_t>(value), width));
}

// Apply one operand pair to a combinational adder model and read the sum back
// as a signed value of the model's declared result width.
template <typename Model>
std::int64_t evaluate(Model& dut, std::int64_t a, std::int64_t b, unsigned operand_width) {
  drive(dut.a, a, operand_width);
  drive(dut.b, b, operand_width);
  dut.eval();
  return minihls::sign_extend(dut.sum,
                              minihls::add_result_width(operand_width, operand_width));
}

// At eight bits the whole input space is only 65536 points, so there is no
// reason to sample it.
TEST(AdderCosim, Width8IsExhaustivelyCorrect) {
  const auto dut = std::make_unique<Vadder8>();
  for (std::int64_t a = -128; a <= 127; ++a) {
    for (std::int64_t b = -128; b <= 127; ++b) {
      ASSERT_EQ(evaluate(*dut, a, b, 8), a + b) << "a=" << a << " b=" << b;
    }
  }
  dut->final();
}

TEST(AdderCosim, Width16MatchesOnRandomInputs) {
  const auto dut = std::make_unique<Vadder16>();
  // Fixed seed: a co-simulation failure is only useful if it reproduces.
  std::mt19937_64 rng(0xC0FFEEULL);
  std::uniform_int_distribution<std::int64_t> operand(minihls::min_value(16, true),
                                                      minihls::max_value(16, true));
  for (int iteration = 0; iteration < 50000; ++iteration) {
    const std::int64_t a = operand(rng);
    const std::int64_t b = operand(rng);
    ASSERT_EQ(evaluate(*dut, a, b, 16), a + b)
        << "iteration=" << iteration << " a=" << a << " b=" << b;
  }
  dut->final();
}

// A dropped carry-out or a missing sign extension shows up at the corners of
// the operand range, which random sampling is unlikely to hit.
TEST(AdderCosim, Width16HandlesRangeCorners) {
  const auto dut = std::make_unique<Vadder16>();
  const std::int64_t lo = minihls::min_value(16, true);
  const std::int64_t hi = minihls::max_value(16, true);
  const std::int64_t corners[] = {lo, lo + 1, -1, 0, 1, hi - 1, hi};
  for (const std::int64_t a : corners) {
    for (const std::int64_t b : corners) {
      ASSERT_EQ(evaluate(*dut, a, b, 16), a + b) << "a=" << a << " b=" << b;
    }
  }
  dut->final();
}

}  // namespace
