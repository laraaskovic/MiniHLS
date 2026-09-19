// MLIRGen, checked by execution: the generated MLIR is lowered to LLVM,
// JIT-compiled, and run on random inputs next to the AST interpreter. Every
// example and every tricky corner of the language must agree bit for bit.

#include "mlirgen/mlirgen.hpp"

#include "generated.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using minihls::testing::Generated;
using minihls::testing::read_example;


// -------------------------------------------------------------------------
// Every example
// -------------------------------------------------------------------------

class ExampleExecution : public ::testing::TestWithParam<const char*> {};

TEST_P(ExampleExecution, MatchesTheInterpreter) {
  Generated generated(read_example(GetParam()));
  ASSERT_TRUE(generated.ok()) << generated.messages();
  EXPECT_GE(generated.compare_on_random_inputs(1000, 0xD07), 900);
}

INSTANTIATE_TEST_SUITE_P(Examples, ExampleExecution,
                         ::testing::Values("abs_diff.hc", "dot.hc", "fir.hc", "max3.hc",
                                           "popcount.hc"),
                         [](const auto& info) {
                           std::string name = info.param;
                           return name.substr(0, name.find('.'));
                         });

// -------------------------------------------------------------------------
// The shape of the generated IR
// -------------------------------------------------------------------------

TEST(MlirGenShape, DotFollowsTheWidthRules) {
  Generated generated(read_example("dot.hc"));
  ASSERT_TRUE(generated.ok()) << generated.messages();
  const std::string text = generated.text();
  // The loop, its carried accumulator, and the pipeline request.
  EXPECT_NE(text.find("scf.for"), std::string::npos) << text;
  EXPECT_NE(text.find("iter_args"), std::string::npos) << text;
  EXPECT_NE(text.find("minihls.pipeline_ii = 1"), std::string::npos) << text;
  // i16 * i16 is computed at 32 bits; the i33 sum is truncated back to i32.
  EXPECT_NE(text.find("arith.muli"), std::string::npos) << text;
  EXPECT_NE(text.find("i32 to i33"), std::string::npos) << text;
  EXPECT_NE(text.find("arith.trunci"), std::string::npos) << text;
}

TEST(MlirGenShape, StreamsWaitForTheirMilestone) {
  Generated generated(read_example("stream_sum.hc"));
  EXPECT_FALSE(generated.ok());
  EXPECT_NE(generated.messages().find("milestone P5"), std::string::npos)
      << generated.messages();
}

// -------------------------------------------------------------------------
// Corners where MLIR's semantics differ from the language's, and control
// flow MLIRGen has to restructure
// -------------------------------------------------------------------------

void expect_agreement(const std::string& source, int vectors = 300) {
  Generated generated(source);
  ASSERT_TRUE(generated.ok()) << generated.messages();
  EXPECT_GT(generated.compare_on_random_inputs(vectors, 0x5EED), 0) << source;
}

TEST(MlirGenSemantics, DivisionByZeroAndOverflow) {
  expect_agreement("i8 f(i8 a, i8 b) { return a / b; }");
  expect_agreement("i8 f(i8 a, i8 b) { return a % b; }");
  expect_agreement("u8 f(u8 a, u8 b) { return a / b; }");
  expect_agreement("u8 f(u8 a, u8 b) { return a % b; }");
  expect_agreement("i16 f(i8 a, u8 b) { return a / b + b % a; }");
  expect_agreement("i64 f(i64 a, i64 b) { return a / b; }");
}

TEST(MlirGenSemantics, ShiftsByAnyAmount) {
  expect_agreement("u8 f(u8 a, u8 k) { return a << k; }");
  expect_agreement("u8 f(u8 a, u8 k) { return a >> k; }");
  expect_agreement("i8 f(i8 a, u8 k) { return a >> k; }");
  expect_agreement("i8 f(i8 a, i8 k) { return a >> k; }");
  expect_agreement("u16 f(u16 a, u2 k) { return a << k; }");
  expect_agreement("u64 f(u64 a, u64 k) { return a >> k; }");
  expect_agreement("u16 f(u8 a) { return a << 8; }");
}

TEST(MlirGenSemantics, MixedSignednessAndWidths) {
  expect_agreement("u1 f(i8 a, u8 b) { return a < b; }");
  expect_agreement("u1 f(i8 a, u8 b) { return a >= b; }");
  expect_agreement("i16 f(u8 a, u8 b) { return a - b; }");
  expect_agreement("i32 f(i16 a, u8 b) { return a * b + -a; }");
  expect_agreement("u8 f(u8 a, i8 b) { return ~a ^ b; }");
  expect_agreement("u8 f(u8 a, i8 b, u1 c) { return c ? a : b; }");
  expect_agreement("u1 f(u8 a, u8 b) { return !a || (a && b); }");
}

TEST(MlirGenControlFlow, EarlyReturns) {
  expect_agreement("i8 f(i8 a) { if (a < 0) { return -a; } return a; }");
  expect_agreement(
      "i8 f(i8 a, i8 b) { if (a < b) { if (a == 0) { return 1; } a = a + 1; } "
      "else { return 2; } return a; }");
  expect_agreement("u8 f(u8 a) { { if (a > 9) { return 9; } } return a; }");
}

TEST(MlirGenControlFlow, IfWithoutElseCarriesValues) {
  expect_agreement("u8 f(u8 a, u8 b) { u8 x = 1; if (a > b) { x = a; b = 3; } return x + b; }");
}

TEST(MlirGenControlFlow, LoopsCarryEveryAssignedVariable) {
  expect_agreement(
      "u16 f(u8 a) { u16 s = 0; u8 last = 0; for (u4 i = 0; i < 10; i = i + 1) "
      "{ if (i & 1) { s = s + a; } else { last = i; } } return s + last; }");
  expect_agreement(
      "u8 f() { u8 i; for (i = 3; i < 100; i = i * 2) { } return i; }");
  // Nested loops, the inner one's variables local to the outer body.
  expect_agreement(
      "u16 f(u8 a) { u16 s = 0; for (u3 i = 0; i < 4; i = i + 1) { u8 t = i; "
      "for (u3 j = 0; j < 3; j = j + 1) { t = t + a; } s = s + t; } return s; }");
}

TEST(MlirGenControlFlow, LoopsThatNeverRun) {
  expect_agreement("u8 f(u8 a) { u8 s = a; for (u4 i = 5; i < 5; i = i + 1) { s = 0; } "
                   "return s; }");
}

TEST(MlirGenArrays, LocalArraysStartFromTheirInitializer) {
  expect_agreement(
      "u8 f(u2 i, u8 v) { u8 t[4] = {1, 2, 3, 4}; t[i] = v; return t[0] + t[3]; }");
}

TEST(MlirGenArrays, RomsAndArrayParameters) {
  expect_agreement(
      "i16 f(u2 i, i16 x[4]) { const i8 k[4] = {-1, 2, -3, 4}; x[i] = x[i] * k[i]; "
      "return x[3 - i]; }");
}

TEST(MlirGenArrays, SignedIndices) {
  // Negative indices are out of bounds; the interpreter rejects those vectors
  // and the rest must agree.
  expect_agreement("u8 f(i3 i, u8 t[4]) { return t[i]; }");
}

}  // namespace
