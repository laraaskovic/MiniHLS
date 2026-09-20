#include <gtest/gtest.h>
#include "support/bits.hpp"

using namespace minihls;

static Bits S(i128 v, unsigned w) { return Bits::make(static_cast<u128>(v), w, true); }
static Bits U(u128 v, unsigned w) { return Bits::make(v, w, false); }
static int64_t val(Bits b) { return static_cast<int64_t>(b.value()); }

// the widths where implementations quietly change behaviour
static constexpr unsigned kWidths[] = {1, 7, 8, 63, 64, 65, 127, 128};

TEST(Bits, MaskCountsBits) {
  EXPECT_EQ(mask(0), static_cast<u128>(0));
  EXPECT_EQ(mask(1), static_cast<u128>(1));
  EXPECT_EQ(mask(8), static_cast<u128>(0xFF));
  EXPECT_EQ(mask(128), ~static_cast<u128>(0));
  EXPECT_EQ(mask(64), static_cast<u128>(0xFFFFFFFFFFFFFFFFull));
}

TEST(Bits, MakeTruncatesAboveTheWidth) {
  EXPECT_EQ(U(0x1FF, 8).raw, static_cast<u128>(0xFF));
  EXPECT_EQ(U(0xFFFF, 1).raw, static_cast<u128>(1));
}

TEST(Bits, AllOnesIsMinusOneSignedAtEveryWidth) {
  for (unsigned w : kWidths)
    EXPECT_EQ(Bits::make(mask(w), w, true).value(), -1) << "width " << w;
}

TEST(Bits, AllOnesIsPositiveUnsignedAtEveryWidth) {
  for (unsigned w : kWidths)
    EXPECT_EQ(Bits::make(mask(w), w, false).raw, mask(w)) << "width " << w;
}

TEST(Bits, OneBitSignedHoldsZeroAndMinusOne) {
  EXPECT_EQ(S(0, 1).value(), 0);
  EXPECT_EQ(S(-1, 1).value(), -1);
}

// arithmetic: widths and values

TEST(Bits, AddGrowsByOneBit) {
  Bits r = add(S(100, 8), S(100, 8));
  EXPECT_EQ(r.width, 9u);
  EXPECT_EQ(val(r), 200);        // would have overflowed i8
  EXPECT_TRUE(r.isSigned);
}

TEST(Bits, AddMixedWidths) {
  Bits r = add(S(-1, 8), S(1000, 16));
  EXPECT_EQ(r.width, 17u);
  EXPECT_EQ(val(r), 999);        // the i8 must sign-extend, not zero-extend
}

TEST(Bits, SubIsAlwaysSigned) {
  Bits r = sub(U(0, 8), U(1, 8));
  EXPECT_EQ(r.width, 9u);
  EXPECT_TRUE(r.isSigned);
  EXPECT_EQ(val(r), -1);         // NOT 511 — this is the rule C gets wrong
}

TEST(Bits, MulSumsWidths) {
  Bits r = mul(S(-128, 8), S(-128, 8));
  EXPECT_EQ(r.width, 16u);
  EXPECT_EQ(val(r), 16384);      // negative * negative is positive
}

TEST(Bits, SignedDivideGrowsSoMinOverMinusOneFits) {
  Bits r = divide(S(-128, 8), S(-1, 8));
  EXPECT_EQ(r.width, 9u);
  EXPECT_EQ(val(r), 128);        // traps if you compute this in i8
}

TEST(Bits, UnsignedDivideKeepsWidth) {
  Bits r = divide(U(200, 8), U(3, 8));
  EXPECT_EQ(r.width, 8u);
  EXPECT_EQ(val(r), 66);         // truncates toward zero
}

TEST(Bits, DivideAndRemainderByZeroAreZero) {
  EXPECT_EQ(val(divide(S(42, 8), S(0, 8))), 0);
  EXPECT_EQ(val(remainder(S(42, 8), S(0, 8))), 0);
}

TEST(Bits, RemainderTakesNarrowerWidthAndSignOfDividend) {
  Bits r = remainder(S(-7, 16), S(3, 8));
  EXPECT_EQ(r.width, 8u);
  EXPECT_EQ(val(r), -1);
}

TEST(Bits, NegateGrowsByOneBit) {
  Bits r = neg(S(-128, 8));
  EXPECT_EQ(r.width, 9u);
  EXPECT_EQ(val(r), 128);
}

// shifts: the defined-behaviour table-

TEST(Bits, ShiftLeftDiscardsHighBitsAndDoesNotGrow) {
  Bits r = shl(U(0xFF, 8), U(4, 8));
  EXPECT_EQ(r.width, 8u);
  EXPECT_EQ(r.raw, static_cast<u128>(0xF0));
}

TEST(Bits, ShiftLeftAtOrBeyondWidthIsZero) {
  for (unsigned amt : {8u, 9u, 99u, 255u})
    EXPECT_EQ(shl(U(0xFF, 8), U(amt, 8)).raw, static_cast<u128>(0)) << amt;
}

TEST(Bits, ShiftRightSignedIsArithmetic) {
  EXPECT_EQ(val(shr(S(-8, 8), U(1, 8))), -4);
}

TEST(Bits, ShiftRightUnsignedIsLogical) {
  EXPECT_EQ(shr(U(0xFF, 8), U(4, 8)).raw, static_cast<u128>(0x0F));
}

TEST(Bits, ShiftRightBeyondWidthSaturates) {
  EXPECT_EQ(val(shr(S(-1, 8), U(99, 8))), -1);   // signed negative -> all ones
  EXPECT_EQ(val(shr(S(5, 8),  U(99, 8))),  0);   // signed positive -> 0
  EXPECT_EQ(shr(U(0xFF, 8), U(99, 8)).raw, static_cast<u128>(0));
}

// casts
TEST(Bits, NarrowingCastKeepsLowBits) {
  Bits r = castTo(S(300, 16), 8, false);
  EXPECT_EQ(r.width, 8u);
  EXPECT_EQ(r.raw, static_cast<u128>(44));       // 300 & 0xFF
}

TEST(Bits, WideningCastExtendsBySourceSignedness) {
  EXPECT_EQ(val(castTo(S(-1, 8), 16, true)), -1);            // sign-extends
  EXPECT_EQ(castTo(U(0xFF, 8), 16, false).raw,
            static_cast<u128>(0xFF));                        // zero-extends
}

TEST(Bits, SameWidthCastReinterprets) {
  EXPECT_EQ(castTo(S(-1, 8), 8, false).raw, static_cast<u128>(0xFF));
  EXPECT_EQ(val(castTo(U(0xFF, 8), 8, true)), -1);
}

// comparison

TEST(Bits, CompareIsSignAware) {
  EXPECT_EQ(compare(S(-1, 8), S(1, 8), Cmp::Lt).raw, static_cast<u128>(1));
  EXPECT_EQ(compare(U(0xFF, 8), U(1, 8), Cmp::Lt).raw, static_cast<u128>(0));
  EXPECT_EQ(compare(S(5, 8), S(5, 8), Cmp::Eq).raw, static_cast<u128>(1));
}

TEST(Bits, CompareProducesU1) {
  Bits r = compare(S(1, 8), S(2, 8), Cmp::Lt);
  EXPECT_EQ(r.width, 1u);
  EXPECT_FALSE(r.isSigned);
}

TEST(Bits, CompareUnsignedAt128BitsDoesNotGoNegative) {
  Bits big = Bits::make(mask(128), 128, false);   // top bit set
  EXPECT_EQ(compare(big, U(1, 128), Cmp::Gt).raw, static_cast<u128>(1));
}