#include "sema/sema.hpp"

#include "checked.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using minihls::testing::Checked;
namespace ast = minihls::ast;

// The type sema assigns to `expression`, evaluated with these parameters in
// scope: a, b are i16; c, d are u8; x is i64; n is u64.
std::string type_of(const std::string& expression) {
  const Checked checked("i64 f(i16 a, i16 b, u8 c, u8 d, i64 x, u64 n) { return " +
                        expression + "; }");
  if (!checked.ok()) return "<error: " + checked.messages() + ">";
  const ast::Stmt& ret = *checked.program().functions.front()->body->statements.front();
  return (ret.value->is_signed ? "i" : "u") + std::to_string(ret.value->width);
}

// Checks a function body with the same parameters and returns the messages.
Checked body(const std::string& statements) {
  return Checked("void f(i16 a, u8 c, i16 arr[8], const i8 rom[4], stream<u8> in, "
                 "stream<u8> out) { " +
                 statements + " }");
}

// -------------------------------------------------------------------------
// The width table in LANGUAGE.md, one row at a time
// -------------------------------------------------------------------------

TEST(WidthRules, LiteralsTakeTheSmallestUnsignedType) {
  EXPECT_EQ(type_of("0"), "u1");
  EXPECT_EQ(type_of("1"), "u1");
  EXPECT_EQ(type_of("5"), "u3");
  EXPECT_EQ(type_of("64"), "u7");
  EXPECT_EQ(type_of("255"), "u8");
}

TEST(WidthRules, AdditionGrowsByOneBit) {
  EXPECT_EQ(type_of("a + b"), "i17");
  EXPECT_EQ(type_of("c + d"), "u9");
}

TEST(WidthRules, SubtractionIsAlwaysSigned) {
  EXPECT_EQ(type_of("a - b"), "i17");
  // u8 - u8 can be negative.
  EXPECT_EQ(type_of("c - d"), "i9");
}

TEST(WidthRules, MixedSignednessWidensTheUnsignedOperand) {
  // u8 is treated as i9 next to a signed operand.
  EXPECT_EQ(type_of("a + c"), "i17");
  EXPECT_EQ(type_of("a * c"), "i25");
  EXPECT_EQ(type_of("a & c"), "i16");
  EXPECT_EQ(type_of("c / a"), "i9");
}

TEST(WidthRules, MultiplicationSumsTheWidths) {
  EXPECT_EQ(type_of("a * b"), "i32");
  EXPECT_EQ(type_of("c * d"), "u16");
}

TEST(WidthRules, DivisionKeepsTheDividendWidth) {
  EXPECT_EQ(type_of("a / b"), "i16");
  EXPECT_EQ(type_of("c % d"), "u8");
}

TEST(WidthRules, BitwiseTakesTheWiderOperand) {
  EXPECT_EQ(type_of("c | d"), "u8");
  EXPECT_EQ(type_of("a ^ b"), "i16");
}

TEST(WidthRules, ConstantLeftShiftGrowsByTheShiftAmount) {
  EXPECT_EQ(type_of("a << 3"), "i19");
  EXPECT_EQ(type_of("a << c"), "i16");
  EXPECT_EQ(type_of("a >> 3"), "i16");
  EXPECT_EQ(type_of("c >> a"), "u8");
}

TEST(WidthRules, UnaryOperators) {
  EXPECT_EQ(type_of("-a"), "i17");
  EXPECT_EQ(type_of("-c"), "i9");
  EXPECT_EQ(type_of("~c"), "u8");
  EXPECT_EQ(type_of("!a"), "u1");
}

TEST(WidthRules, ComparisonsAndLogicalOperatorsAreOneBit) {
  EXPECT_EQ(type_of("a < c"), "u1");
  EXPECT_EQ(type_of("a == b"), "u1");
  EXPECT_EQ(type_of("a && c"), "u1");
  EXPECT_EQ(type_of("a || b"), "u1");
}

TEST(WidthRules, ConditionalDoesNotAddABit) {
  EXPECT_EQ(type_of("c ? a : b"), "i16");
  // d is widened to i9 next to the signed a.
  EXPECT_EQ(type_of("c ? a : d"), "i16");
  EXPECT_EQ(type_of("a ? c : d"), "u8");
}

TEST(WidthRules, TheDocumentedWorkedExample) {
  // acc + x*y: i16*i16 is i32, and i32 + i32 is i33, truncated back to i32 by
  // the return.
  const Checked checked("i32 f(i32 acc, i16 x, i16 y) { return acc + x * y; }");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  const ast::Expr& sum =
      *checked.program().functions.front()->body->statements.front()->value;
  EXPECT_EQ(sum.width, 33u);
  EXPECT_TRUE(sum.is_signed);
  EXPECT_EQ(sum.rhs->width, 32u);
  ASSERT_EQ(checked.function().truncations.size(), 1u);
}

TEST(WidthRules, RejectsResultsWiderThan64Bits) {
  const std::string result = type_of("x * x");
  EXPECT_NE(result.find("128 bits"), std::string::npos) << result;
  EXPECT_NE(type_of("n + n").find("65 bits"), std::string::npos);
  // u64 next to a signed value would need i65.
  EXPECT_NE(type_of("n < a").find("65 bits"), std::string::npos);
}

TEST(WidthRules, FoldsConstantExpressions) {
  const Checked checked("i32 f() { return 3 * 5 - 20; }");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  const ast::Expr& value =
      *checked.program().functions.front()->body->statements.front()->value;
  ASSERT_TRUE(value.is_constant);
  // 3 * 5 is u5 15; 15 - 20 is i6 -5, whose six-bit pattern is 0b111011.
  EXPECT_EQ(value.width, 6u);
  EXPECT_EQ(value.constant, 0b111011u);
}

// -------------------------------------------------------------------------
// Names and scopes
// -------------------------------------------------------------------------

TEST(SemaNames, RejectsUndeclaredNames) {
  const Checked checked = body("a = missing;");
  EXPECT_FALSE(checked.ok());
  EXPECT_TRUE(checked.mentions("use of undeclared name 'missing'"));
}

TEST(SemaNames, RejectsShadowing) {
  const Checked checked = body("{ i8 a = 0; }");
  EXPECT_FALSE(checked.ok());
  EXPECT_TRUE(checked.mentions("shadows an outer declaration"));
}

TEST(SemaNames, RejectsRedeclarationInOneScope) {
  const Checked checked = body("i8 x = 0; i8 x = 1;");
  EXPECT_FALSE(checked.ok());
  EXPECT_TRUE(checked.mentions("'x' is already declared in this scope"));
}

TEST(SemaNames, ScopesEndWithTheirBlock) {
  const Checked checked = body("{ i8 x = 0; } { i8 x = 1; }");
  EXPECT_TRUE(checked.ok()) << checked.messages();
}

TEST(SemaNames, ArraysAndStreamsAreNotValues) {
  EXPECT_TRUE(body("a = arr;").mentions("array 'arr' is not a value"));
  EXPECT_TRUE(body("a = in;").mentions("can only be used with read() or write()"));
}

// -------------------------------------------------------------------------
// Assignments, arrays, and returns
// -------------------------------------------------------------------------

TEST(SemaAssign, RejectsWritesToConstArrays) {
  EXPECT_TRUE(body("rom[0] = 1;").mentions("which is a ROM"));
}

TEST(SemaAssign, RejectsConstantIndicesOutOfBounds) {
  EXPECT_TRUE(body("a = arr[8];").mentions("index 8 is out of bounds"));
  EXPECT_TRUE(body("arr[9] = 1;").mentions("index 9 is out of bounds"));
  EXPECT_TRUE(body("a = arr[-1];").mentions("index -1 is out of bounds"));
}

TEST(SemaAssign, RejectsArraysOutsideTheTopLevel) {
  EXPECT_TRUE(body("if (a) { i8 local[4]; }").mentions("top level of the function body"));
  EXPECT_TRUE(body("i8 local[4];").ok());
}

TEST(SemaAssign, RejectsTooManyInitializers) {
  EXPECT_TRUE(body("const i8 t[2] = {1, 2, 3};").mentions("too many initializers"));
}

TEST(SemaAssign, RequiresConstantArrayInitializers) {
  EXPECT_TRUE(body("const i8 t[2] = {a, 2};").mentions("compile-time constant"));
}

TEST(SemaAssign, ZeroFillsShortInitializers) {
  const Checked checked = body("const i8 t[4] = {-1, 2};");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  const auto& symbols = checked.function().symbols;
  const auto& table = symbols.back();
  ASSERT_EQ(table.name, "t");
  EXPECT_EQ(table.initial, (std::vector<std::uint64_t>{0xFF, 2, 0, 0}));
}

TEST(SemaReturn, RequiresEveryPathToReturn) {
  const Checked missing("i8 f(u1 c) { if (c) { return 1; } }");
  EXPECT_TRUE(missing.mentions("control can reach the end"));
  const Checked both("i8 f(u1 c) { if (c) { return 1; } else { return 2; } }");
  EXPECT_TRUE(both.ok()) << both.messages();
}

TEST(SemaReturn, ChecksReturnAgainstVoid) {
  EXPECT_TRUE(Checked("void f() { return 1; }").mentions("cannot return a value"));
  EXPECT_TRUE(Checked("i8 f() { return; }").mentions("must return a value"));
}

TEST(SemaReturn, RejectsReturnInsideALoop) {
  const Checked checked("i8 f() { for (u2 i = 0; i < 2; i = i + 1) { return 1; } return 0; }");
  EXPECT_TRUE(checked.mentions("return inside a loop"));
}

TEST(SemaReturn, RecordsTruncations) {
  const Checked checked("u8 f(u8 a) { u8 b = a + a; return b; }");
  ASSERT_TRUE(checked.ok());
  ASSERT_EQ(checked.function().truncations.size(), 1u);
  EXPECT_NE(checked.function().truncations[0].message.find("u9 to u8"), std::string::npos);
}

// -------------------------------------------------------------------------
// Streams
// -------------------------------------------------------------------------

TEST(SemaStreams, InfersDirectionFromUse) {
  const Checked checked = body("u8 x = read(in); write(out, x);");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  const auto& symbols = checked.function().symbols;
  EXPECT_EQ(symbols[4].direction, minihls::sema::StreamDirection::Input);
  EXPECT_EQ(symbols[5].direction, minihls::sema::StreamDirection::Output);
}

TEST(SemaStreams, RejectsAStreamUsedBothWays) {
  EXPECT_TRUE(body("u8 x = read(in); write(in, x);").mentions("both read and written"));
}

TEST(SemaStreams, RejectsReadsInsideLogicalOperators) {
  EXPECT_TRUE(body("u1 x = a && read(in);").mentions("cannot be an operand of '&&'"));
  EXPECT_TRUE(body("u8 x = a ? read(in) : 0;").mentions("branch of '?:'"));
}

TEST(SemaStreams, RejectsOtherCalls) {
  EXPECT_TRUE(body("a = write(out, 1);").mentions("write() does not produce a value"));
  EXPECT_TRUE(body("a = helper(1);").mentions("unknown function 'helper'"));
}

// -------------------------------------------------------------------------
// Loops and pragmas
// -------------------------------------------------------------------------

std::uint64_t only_trip_count(const Checked& checked) {
  EXPECT_TRUE(checked.ok()) << checked.messages();
  EXPECT_EQ(checked.function().loops.size(), 1u);
  return checked.function().loops.begin()->second.trip_count;
}

TEST(SemaLoops, ComputesExactTripCounts) {
  EXPECT_EQ(only_trip_count(body("for (u7 i = 0; i < 64; i = i + 1) { a = a; }")), 64u);
  EXPECT_EQ(only_trip_count(body("for (u4 i = 7; i > 0; i = i - 1) { a = a; }")), 7u);
  EXPECT_EQ(only_trip_count(body("for (u8 i = 0; i < 100; i = i + 3) { a = a; }")), 34u);
  EXPECT_EQ(only_trip_count(body("for (u8 i = 5; i < 5; i = i + 1) { a = a; }")), 0u);
}

TEST(SemaLoops, RequiresConstantBounds) {
  EXPECT_TRUE(body("for (u8 i = 0; i < c; i = i + 1) { a = a; }")
                  .mentions("must depend only on 'i' and constants"));
  EXPECT_TRUE(body("for (u8 i = c; i < 4; i = i + 1) { a = a; }")
                  .mentions("initial value must be a compile-time constant"));
}

TEST(SemaLoops, CatchesLoopsThatNeverFinish) {
  // A u8 is always below 300, so this wraps forever.
  EXPECT_TRUE(body("for (u8 i = 0; i < 300; i = i + 1) { a = a; }")
                  .mentions("does not finish"));
}

TEST(SemaLoops, ProtectsTheInductionVariable) {
  EXPECT_TRUE(body("for (u8 i = 0; i < 4; i = i + 1) { i = 2; }")
                  .mentions("may not assign the induction variable"));
  EXPECT_TRUE(body("for (u8 i = 0; i < 4; i = i + 1) { a = a; }").ok());
}

TEST(SemaLoops, RequiresTheStepToAssignTheInductionVariable) {
  EXPECT_TRUE(body("u8 j = 0; for (u8 i = 0; i < 4; j = j + 1) { a = a; }")
                  .mentions("step must assign the induction variable 'i'"));
}

TEST(SemaPragmas, BindsPipelineAndUnrollToTheEnclosingLoop) {
  const Checked checked = body(
      "for (u3 i = 0; i < 4; i = i + 1) { #pragma pipeline II=2\n a = a; }"
      "for (u3 j = 0; j < 4; j = j + 1) { #pragma unroll factor=2\n a = a; }");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  int pipelined = 0;
  int unrolled = 0;
  for (const auto& [stmt, loop] : checked.function().loops) {
    if (loop.pipeline_ii == 2) ++pipelined;
    if (loop.unroll_factor == 2) ++unrolled;
  }
  EXPECT_EQ(pipelined, 1);
  EXPECT_EQ(unrolled, 1);
}

TEST(SemaPragmas, CompleteUnrollRecordsEveryInductionValue) {
  const Checked checked = body("for (u4 i = 7; i > 2; i = i - 2) { #pragma unroll\n a = a; }");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  const auto& loop = checked.function().loops.begin()->second;
  EXPECT_EQ(loop.unroll_factor, 3u);
  EXPECT_EQ(loop.induction_values, (std::vector<std::uint64_t>{7, 5, 3}));
  EXPECT_EQ(loop.final_value, 1u);
}

TEST(SemaLoops, WrapAroundCanMakeALoopEndless) {
  // After 1, `i - 2` is -1, which a u4 holds as 15: the odd values never reach
  // zero, so this loop has no trip count.
  EXPECT_TRUE(body("for (u4 i = 7; i > 0; i = i - 2) { a = a; }").mentions("does not finish"));
}

TEST(SemaPragmas, RejectsMisplacedPragmas) {
  EXPECT_TRUE(body("#pragma pipeline II=1\n").mentions("must appear inside the body"));
  EXPECT_TRUE(body("for (u3 i = 0; i < 6; i = i + 1) { #pragma unroll factor=4\n a = a; }")
                  .mentions("does not divide the trip count 6"));
  EXPECT_TRUE(body("for (u3 i = 0; i < 4; i = i + 1) { #pragma unroll\n #pragma pipeline "
                   "II=1\n a = a; }")
                  .mentions("both unrolled and pipelined"));
  EXPECT_TRUE(body("#pragma partition arr\n").mentions("cannot partition parameter"));
  EXPECT_TRUE(body("#pragma partition nothing\n").mentions("no array named 'nothing'"));
}

TEST(SemaPragmas, PartitionMarksTheArray) {
  const Checked checked = body("const i8 t[4] = {1, 2, 3, 4};\n#pragma partition t\n");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  EXPECT_TRUE(checked.function().symbols.back().partitioned);
}

}  // namespace
