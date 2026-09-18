#include "support/bits.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace {

TEST(MaskForWidth, CoversNarrowestAndWidestCases) {
  EXPECT_EQ(minihls::mask_for_width(1), std::uint64_t{0x1});
  EXPECT_EQ(minihls::mask_for_width(8), std::uint64_t{0xff});
  EXPECT_EQ(minihls::mask_for_width(63), (std::uint64_t{1} << 63) - 1);
  // The widest mask cannot be built by shifting, so it gets its own path.
  EXPECT_EQ(minihls::mask_for_width(64), ~std::uint64_t{0});
}

TEST(Truncate, DropsBitsAboveTheWidth) {
  EXPECT_EQ(minihls::truncate(0x1ff, 8), std::uint64_t{0xff});
  EXPECT_EQ(minihls::truncate(0x100, 8), std::uint64_t{0x00});
  EXPECT_EQ(minihls::truncate(0xdeadbeef, 64), std::uint64_t{0xdeadbeef});
}

TEST(SignExtend, ReinterpretsTheTopBitAsASign) {
  EXPECT_EQ(minihls::sign_extend(0x7f, 8), 127);
  EXPECT_EQ(minihls::sign_extend(0x80, 8), -128);
  EXPECT_EQ(minihls::sign_extend(0xff, 8), -1);
  // A one-bit signed value holds only -1 and 0.
  EXPECT_EQ(minihls::sign_extend(0x1, 1), -1);
  EXPECT_EQ(minihls::sign_extend(0x0, 1), 0);
}

TEST(SignExtend, IgnoresBitsAboveTheWidth) {
  EXPECT_EQ(minihls::sign_extend(0xff80, 8), -128);
  EXPECT_EQ(minihls::sign_extend(0xab7f, 8), 127);
}

TEST(ZeroExtend, KeepsTheTopBitAsMagnitude) {
  EXPECT_EQ(minihls::zero_extend(0x80, 8), std::uint64_t{128});
  EXPECT_EQ(minihls::zero_extend(0xff80, 8), std::uint64_t{0x80});
}

TEST(ValueRange, MatchesTheDeclaredWidths) {
  EXPECT_EQ(minihls::min_value(8, true), -128);
  EXPECT_EQ(minihls::max_value(8, true), 127);
  EXPECT_EQ(minihls::min_value(8, false), 0);
  EXPECT_EQ(minihls::max_value(8, false), 255);
  // u7 is the loop counter type used by the examples.
  EXPECT_EQ(minihls::max_value(7, false), 127);
  EXPECT_EQ(minihls::min_value(64, true), std::numeric_limits<std::int64_t>::min());
  EXPECT_EQ(minihls::max_value(64, true), std::numeric_limits<std::int64_t>::max());
}

TEST(WidthRules, ArithmeticGrowsAsDocumented) {
  EXPECT_EQ(minihls::add_result_width(8, 8), 9u);
  EXPECT_EQ(minihls::add_result_width(8, 16), 17u);
  EXPECT_EQ(minihls::add_result_width(16, 8), 17u);
  EXPECT_EQ(minihls::add_result_width(1, 1), 2u);
  EXPECT_EQ(minihls::mul_result_width(16, 16), 32u);
  EXPECT_EQ(minihls::mul_result_width(8, 4), 12u);
}

// The point of the width rules is that the widened result can represent every
// outcome, so check that against the actual operand ranges rather than trusting
// the formula.
TEST(WidthRules, AdditionResultHoldsEveryOperandPair) {
  for (unsigned w = 1; w <= 24; ++w) {
    const unsigned result_width = minihls::add_result_width(w, w);
    for (const bool is_signed : {false, true}) {
      const std::int64_t lo = minihls::min_value(w, is_signed);
      const std::int64_t hi = minihls::max_value(w, is_signed);
      EXPECT_GE(lo + lo, minihls::min_value(result_width, is_signed)) << "w=" << w;
      EXPECT_LE(hi + hi, minihls::max_value(result_width, is_signed)) << "w=" << w;
    }
  }
}

TEST(WidthRules, MultiplicationResultHoldsEveryOperandPair) {
  for (unsigned w = 1; w <= 24; ++w) {
    const unsigned result_width = minihls::mul_result_width(w, w);
    for (const bool is_signed : {false, true}) {
      const std::int64_t lo = minihls::min_value(w, is_signed);
      const std::int64_t hi = minihls::max_value(w, is_signed);
      const std::int64_t result_lo = minihls::min_value(result_width, is_signed);
      const std::int64_t result_hi = minihls::max_value(result_width, is_signed);
      // Every corner of the operand box, since the extreme product of two
      // signed ranges is lo * lo rather than hi * hi.
      for (const std::int64_t a : {lo, hi}) {
        for (const std::int64_t b : {lo, hi}) {
          EXPECT_GE(a * b, result_lo) << "w=" << w;
          EXPECT_LE(a * b, result_hi) << "w=" << w;
        }
      }
    }
  }
}

TEST(BitsNeeded, CountsUnsignedValues) {
  EXPECT_EQ(minihls::bits_needed(0, false), 1u);
  EXPECT_EQ(minihls::bits_needed(1, false), 1u);
  EXPECT_EQ(minihls::bits_needed(2, false), 2u);
  EXPECT_EQ(minihls::bits_needed(255, false), 8u);
  EXPECT_EQ(minihls::bits_needed(256, false), 9u);
}

TEST(BitsNeeded, CountsSignedValuesIncludingTheSignBit) {
  EXPECT_EQ(minihls::bits_needed(0, true), 1u);
  EXPECT_EQ(minihls::bits_needed(-1, true), 1u);
  EXPECT_EQ(minihls::bits_needed(1, true), 2u);
  EXPECT_EQ(minihls::bits_needed(127, true), 8u);
  // The negative range reaches one further than the positive one.
  EXPECT_EQ(minihls::bits_needed(-128, true), 8u);
  EXPECT_EQ(minihls::bits_needed(-129, true), 9u);
}

TEST(BitsNeeded, RoundTripsWithValueRange) {
  for (unsigned w = 1; w <= 32; ++w) {
    EXPECT_EQ(minihls::bits_needed(minihls::min_value(w, true), true), w) << "w=" << w;
    EXPECT_EQ(minihls::bits_needed(minihls::max_value(w, true), true), w) << "w=" << w;
    EXPECT_EQ(minihls::bits_needed(minihls::max_value(w, false), false), w) << "w=" << w;
  }
}

}  // namespace
