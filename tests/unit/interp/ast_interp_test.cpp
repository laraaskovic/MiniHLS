#include "interp/ast_interp.hpp"

#include "../sema/checked.hpp"
#include "support/bits.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using minihls::interp::Inputs;
using minihls::interp::Outputs;
using minihls::testing::Checked;

std::string read_example(const std::string& name) {
  const std::filesystem::path path = std::filesystem::path(MINIHLS_EXAMPLES_DIR) / name;
  std::ifstream stream(path);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

// Two's complement bits of a negative test value at the given width.
std::uint64_t bits(std::int64_t value, unsigned width) {
  return minihls::truncate(static_cast<std::uint64_t>(value), width);
}

// Runs the only function in `source` with scalar arguments.
Outputs run(const Checked& checked, std::vector<std::uint64_t> scalars) {
  EXPECT_TRUE(checked.ok()) << checked.messages();
  Inputs inputs;
  const std::size_t count = checked.function().params.size();
  scalars.resize(count, 0);
  inputs.scalars = std::move(scalars);
  inputs.arrays.assign(count, {});
  inputs.streams.assign(count, {});
  return minihls::interp::run_ast(checked.function(), inputs);
}

// The return value of a one-expression function over the given parameters.
std::uint64_t evaluate(const std::string& signature, const std::string& expression,
                       std::vector<std::uint64_t> scalars) {
  const Checked checked(signature + " { return " + expression + "; }");
  const Outputs outputs = run(checked, std::move(scalars));
  EXPECT_TRUE(outputs.ok()) << outputs.error;
  return outputs.return_value.value_or(~std::uint64_t{0});
}

// -------------------------------------------------------------------------
// Every example against hand-computed results
// -------------------------------------------------------------------------

TEST(Examples, Max3) {
  const Checked checked(read_example("max3.hc"));
  EXPECT_EQ(run(checked, {3, bits(-9, 16), 7}).return_value, 7u);
  EXPECT_EQ(run(checked, {bits(-1, 16), bits(-2, 16), bits(-3, 16)}).return_value,
            bits(-1, 16));
  EXPECT_EQ(run(checked, {5, 5, 5}).return_value, 5u);
}

TEST(Examples, AbsDiff) {
  const Checked checked(read_example("abs_diff.hc"));
  EXPECT_EQ(run(checked, {10, 3}).return_value, 7u);
  EXPECT_EQ(run(checked, {3, 10}).return_value, 7u);
  // The extreme case is why the difference needs 17 bits.
  EXPECT_EQ(run(checked, {bits(-32768, 16), 32767}).return_value, 65535u);
}

TEST(Examples, Popcount) {
  const Checked checked(read_example("popcount.hc"));
  EXPECT_EQ(run(checked, {0}).return_value, 0u);
  EXPECT_EQ(run(checked, {0xFFFF}).return_value, 16u);
  EXPECT_EQ(run(checked, {0b1011}).return_value, 3u);
  EXPECT_EQ(run(checked, {0x8001}).return_value, 2u);
}

TEST(Examples, DotProduct) {
  const Checked checked(read_example("dot.hc"));
  ASSERT_TRUE(checked.ok()) << checked.messages();
  Inputs inputs;
  inputs.scalars.assign(2, 0);
  inputs.streams.assign(2, {});
  std::vector<std::uint64_t> a(64);
  std::vector<std::uint64_t> b(64);
  std::int64_t expected = 0;
  for (std::int64_t i = 0; i < 64; ++i) {
    const std::int64_t x = (i % 2 == 0) ? -32768 : 500 * i;
    const std::int64_t y = -32768 + 1000 * i;
    a[static_cast<std::size_t>(i)] = bits(x, 16);
    b[static_cast<std::size_t>(i)] = bits(y, 16);
    expected += x * y;
  }
  inputs.arrays = {a, b};
  const Outputs outputs = minihls::interp::run_ast(checked.function(), inputs);
  ASSERT_TRUE(outputs.ok()) << outputs.error;
  // The accumulator is i32, so the exact sum wraps to 32 bits.
  EXPECT_EQ(outputs.return_value, bits(expected, 32));
}

TEST(Examples, FirShiftsTheHistoryAndFilters) {
  const Checked checked(read_example("fir.hc"));
  ASSERT_TRUE(checked.ok()) << checked.messages();
  Inputs inputs;
  inputs.scalars = {10, 0};
  inputs.arrays = {{}, {1, 2, 3, 4, 5, 6, 7, 8}};
  inputs.streams.assign(2, {});
  const Outputs outputs = minihls::interp::run_ast(checked.function(), inputs);
  ASSERT_TRUE(outputs.ok()) << outputs.error;
  EXPECT_EQ(outputs.arrays[1], (std::vector<std::uint64_t>{10, 1, 2, 3, 4, 5, 6, 7}));
  // 10*3 + 1*-7 + 2*12 + 3*31 + 4*31 + 5*12 + 6*-7 + 7*3
  EXPECT_EQ(outputs.return_value, 303u);
}

TEST(Examples, StreamSum) {
  const Checked checked(read_example("stream_sum.hc"));
  ASSERT_TRUE(checked.ok()) << checked.messages();
  Inputs inputs;
  inputs.scalars.assign(2, 0);
  inputs.arrays.assign(2, {});
  inputs.streams = {std::vector<std::uint64_t>(256, 255), {}};
  const Outputs outputs = minihls::interp::run_ast(checked.function(), inputs);
  ASSERT_TRUE(outputs.ok()) << outputs.error;
  EXPECT_EQ(outputs.consumed[0], 256u);
  EXPECT_EQ(outputs.streams[1], (std::vector<std::uint64_t>{65280}));
}

// -------------------------------------------------------------------------
// Semantics that are easy to get wrong
// -------------------------------------------------------------------------

TEST(Semantics, AssignmentTruncatesAndExtends) {
  // 300 keeps its low 8 bits.
  EXPECT_EQ(evaluate("u8 f()", "300", {}), 44u);
  // A signed source sign-extends; an unsigned one zero-extends.
  EXPECT_EQ(evaluate("i32 f(i8 a)", "a", {bits(-1, 8)}), bits(-1, 32));
  EXPECT_EQ(evaluate("i32 f(u8 a)", "a", {0xFF}), 255u);
}

TEST(Semantics, SignedDivisionTruncatesTowardZero) {
  EXPECT_EQ(evaluate("i8 f(i8 a, i8 b)", "a / b", {bits(-7, 8), 2}), bits(-3, 8));
  EXPECT_EQ(evaluate("i8 f(i8 a, i8 b)", "a % b", {bits(-7, 8), 2}), bits(-1, 8));
  // The one overflowing quotient wraps.
  EXPECT_EQ(evaluate("i8 f(i8 a, i8 b)", "a / b", {bits(-128, 8), bits(-1, 8)}),
            bits(-128, 8));
}

TEST(Semantics, DivisionByZeroIsDefined) {
  EXPECT_EQ(evaluate("u8 f(u8 a, u8 b)", "a / b", {7, 0}), 0xFFu);
  EXPECT_EQ(evaluate("u8 f(u8 a, u8 b)", "a % b", {7, 0}), 7u);
  EXPECT_EQ(evaluate("i8 f(i8 a, i8 b)", "a / b", {7, 0}), bits(-1, 8));
}

TEST(Semantics, ShiftsPastTheWidth) {
  EXPECT_EQ(evaluate("u8 f(u8 a, u8 k)", "a << k", {0xFF, 8}), 0u);
  EXPECT_EQ(evaluate("u8 f(u8 a, u8 k)", "a >> k", {0xFF, 9}), 0u);
  // An arithmetic shift fills with the sign bit.
  EXPECT_EQ(evaluate("i8 f(i8 a, u8 k)", "a >> k", {bits(-128, 8), 200}), bits(-1, 8));
  EXPECT_EQ(evaluate("i8 f(i8 a, u8 k)", "a >> k", {bits(-128, 8), 2}), bits(-32, 8));
  // A constant left shift grows instead of dropping bits.
  EXPECT_EQ(evaluate("u16 f(u8 a)", "a << 4", {0xFF}), 0xFF0u);
}

TEST(Semantics, MixedSignednessComparesMathematically) {
  // In C, -1 < 255u is false after conversion; here it is simply true.
  EXPECT_EQ(evaluate("u1 f(i8 a, u8 b)", "a < b", {bits(-1, 8), 255}), 1u);
  EXPECT_EQ(evaluate("u1 f(i8 a, u8 b)", "a == b", {bits(-1, 8), 255}), 0u);
}

TEST(Semantics, SubtractionOfUnsignedValuesCanBeNegative) {
  EXPECT_EQ(evaluate("i16 f(u8 a, u8 b)", "a - b", {0, 255}), bits(-255, 16));
}

TEST(Semantics, LogicalOperatorsAreBooleans) {
  EXPECT_EQ(evaluate("u1 f(u8 a, u8 b)", "a && b", {2, 4}), 1u);
  EXPECT_EQ(evaluate("u1 f(u8 a, u8 b)", "a || b", {0, 0}), 0u);
  EXPECT_EQ(evaluate("u8 f(u8 a)", "!a", {9}), 0u);
}

TEST(Semantics, UninitializedScalarsAreZero) {
  const Checked checked("u8 f() { u8 x; return x; }");
  EXPECT_EQ(run(checked, {}).return_value, 0u);
}

TEST(Semantics, LocalArraysStartFromTheirInitializerOnEveryCall) {
  const Checked checked(
      "u8 f(u2 i) { u8 t[4] = {1, 2, 3, 4}; t[i] = t[i] + 10; return t[i] + t[3]; }");
  EXPECT_EQ(run(checked, {0}).return_value, 15u);
  // The write from the previous call must not persist.
  EXPECT_EQ(run(checked, {0}).return_value, 15u);
  EXPECT_EQ(run(checked, {3}).return_value, 28u);
}

TEST(Semantics, OutOfBoundsAccessIsARuntimeError) {
  const Checked checked("i8 f(u4 i, i8 t[8]) { return t[i]; }");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  Inputs inputs;
  inputs.scalars = {9, 0};
  inputs.arrays = {{}, std::vector<std::uint64_t>(8, 0)};
  inputs.streams.assign(2, {});
  const Outputs outputs = minihls::interp::run_ast(checked.function(), inputs);
  EXPECT_NE(outputs.error.find("out of bounds"), std::string::npos) << outputs.error;
}

TEST(Semantics, ReadingPastTheEndOfAStreamIsARuntimeError) {
  const Checked checked("u8 f(stream<u8> s) { u8 a = read(s); u8 b = read(s); return b; }");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  Inputs inputs;
  inputs.scalars = {0};
  inputs.arrays = {{}};
  inputs.streams = {{42}};
  const Outputs outputs = minihls::interp::run_ast(checked.function(), inputs);
  EXPECT_NE(outputs.error.find("read past the end"), std::string::npos) << outputs.error;
}

TEST(Semantics, OperandsAreEvaluatedLeftToRight) {
  const Checked checked("i16 f(stream<u8> s) { return read(s) - read(s); }");
  ASSERT_TRUE(checked.ok()) << checked.messages();
  Inputs inputs;
  inputs.scalars = {0};
  inputs.arrays = {{}};
  inputs.streams = {{10, 3}};
  EXPECT_EQ(minihls::interp::run_ast(checked.function(), inputs).return_value, 7u);
}

}  // namespace
