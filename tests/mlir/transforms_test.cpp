// The optimization pipeline (milestone P4).
//
// The safety argument for every pass here is the same one MLIRGen's tests
// make, applied after each step: compile the module, run it on inputs whose
// answers the AST interpreter already gave, and require agreement. A pass that
// changes what the program computes fails the test and names itself while
// doing it.
//
// On top of that are the effect tests -- unrolling really removes the loop,
// narrowing really shrinks the accumulator -- because a pass that is correct
// but does nothing would pass every check above.

#include "transforms/transforms.hpp"

#include "generated.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using minihls::testing::Generated;
using minihls::testing::read_example;
using minihls::testing::Vector;

// Fewer vectors than MLIRGen's suite uses, because these are re-run after each
// of the ten passes and each re-run pays for a fresh JIT compilation.
constexpr int kVectors = 128;

// Runs the pipeline over `generated`, checking behaviour after every pass.
// Returns the first complaint, or an empty string.
std::string optimize_and_check(Generated& generated) {
  const std::vector<Vector> vectors = generated.sample(kVectors, 0xC0FFEE);
  if (vectors.empty()) return "no input vector was accepted by the interpreter";

  std::string complaint = generated.disagreement(vectors, "before any pass");
  if (!complaint.empty()) return complaint;

  const bool ok = minihls::transforms::optimize(
      generated.module(), [&](std::string_view pass) {
        // Only the first failure is reported: once a pass has broken the
        // program, every later one inherits the damage.
        if (complaint.empty()) complaint = generated.disagreement(vectors, pass);
      });
  if (!ok) return "a pass reported failure";
  return complaint;
}

// ---------------------------------------------------------------------------
// Behaviour is preserved, pass by pass, on every example
// ---------------------------------------------------------------------------

class ExampleOptimization : public ::testing::TestWithParam<const char*> {};

TEST_P(ExampleOptimization, EveryPassPreservesBehaviour) {
  Generated generated(read_example(GetParam()));
  ASSERT_TRUE(generated.ok()) << generated.messages();
  EXPECT_EQ(optimize_and_check(generated), "") << generated.text();
}

INSTANTIATE_TEST_SUITE_P(Examples, ExampleOptimization,
                         ::testing::Values("abs_diff.hc", "dot.hc", "fir.hc", "max3.hc",
                                           "popcount.hc"),
                         [](const auto& info) {
                           std::string name = info.param;
                           return name.substr(0, name.find('.'));
                         });

// The narrowing pattern is the one place a wrong answer is plausible, so it
// gets the operators it claims to be exact for, at widths where a dropped high
// bit would show, and the ones it refuses to touch beside them.
void expect_preserved(const std::string& source) {
  Generated generated(source);
  ASSERT_TRUE(generated.ok()) << generated.messages();
  EXPECT_EQ(optimize_and_check(generated), "") << source << "\n" << generated.text();
}

TEST(Narrowing, IsExactForTheOperatorsItClaims) {
  // Each of these widens to compute and truncates on assignment, which is
  // exactly the shape the pattern matches.
  expect_preserved("u8 f(u8 a, u8 b) { return a + b; }");
  expect_preserved("u8 f(u8 a, u8 b) { return a - b; }");
  expect_preserved("u8 f(u8 a, u8 b) { return a * b; }");
  expect_preserved("i8 f(i8 a, i8 b) { return a + b; }");
  expect_preserved("i8 f(i8 a, i8 b) { return a * b; }");
  expect_preserved("u8 f(u8 a, i8 b) { return (a & b) | (a ^ b); }");
  // Mixed signedness: the operands reach the wide type through different
  // extensions, and the narrowed form has to agree with both.
  expect_preserved("i8 f(i8 a, u8 b) { return a + b; }");
  expect_preserved("u16 f(u8 a, i16 b) { return a * b; }");
  // Chains, where narrowing one operation exposes the next.
  expect_preserved("u8 f(u8 a, u8 b, u8 c) { return a * b + c * a; }");
  expect_preserved("u4 f(u8 a, u8 b) { return a + b; }");
}

TEST(Narrowing, LeavesAloneWhatItCannotProve) {
  // Division, remainder, shifts, and comparisons all read high bits, so
  // narrowing them would change the answer. They must survive unchanged.
  expect_preserved("u8 f(u8 a, u8 b) { return a / b; }");
  expect_preserved("u8 f(u8 a, u8 b) { return a % b; }");
  expect_preserved("u8 f(u8 a, u8 b) { return (a + b) / 3; }");
  expect_preserved("u8 f(u8 a, u8 k) { return (a + a) >> k; }");
  expect_preserved("u1 f(u8 a, u8 b) { return a + b > 200; }");
}

// ---------------------------------------------------------------------------
// The passes actually do something
// ---------------------------------------------------------------------------

TEST(Narrowing, ShrinksTheDotProductAccumulator) {
  Generated generated(read_example("dot.hc"));
  ASSERT_TRUE(generated.ok()) << generated.messages();
  // i32 + i32 grows to i33 by the language's width rule, and the assignment
  // back to `acc` truncates it. Nothing outside reads the i33 value, so the
  // adder can be built at 32 bits.
  EXPECT_NE(generated.text().find("i33"), std::string::npos) << generated.text();

  ASSERT_TRUE(minihls::transforms::optimize(generated.module()));
  EXPECT_EQ(generated.text().find("i33"), std::string::npos) << generated.text();
}

TEST(Narrowing, ReportsFewerDatapathBits) {
  Generated generated(read_example("dot.hc"));
  ASSERT_TRUE(generated.ok()) << generated.messages();
  const auto before = minihls::transforms::measure(generated.module());
  ASSERT_TRUE(minihls::transforms::optimize(generated.module()));
  const auto after = minihls::transforms::measure(generated.module());

  EXPECT_LT(after.bits, before.bits);
  // The report names every operator either side has and is stable enough to
  // paste into a commit message.
  const std::string report = minihls::transforms::format_report(before, after);
  EXPECT_NE(report.find("arith.muli"), std::string::npos) << report;
  EXPECT_NE(report.find("datapath bits"), std::string::npos) << report;
}

TEST(Unrolling, RemovesTheLoopWhenTheFactorReachesTheTripCount) {
  Generated generated(read_example("popcount.hc"));
  ASSERT_TRUE(generated.ok()) << generated.messages();
  EXPECT_NE(generated.text().find("scf.for"), std::string::npos) << generated.text();
  EXPECT_NE(generated.text().find("minihls.unroll"), std::string::npos) << generated.text();

  ASSERT_TRUE(minihls::transforms::optimize(generated.module()));
  const std::string text = generated.text();
  // Sixteen iterations, fully unrolled: no loop, and no marker left behind for
  // a second run to act on.
  EXPECT_EQ(text.find("scf.for"), std::string::npos) << text;
  EXPECT_EQ(text.find("minihls.unroll"), std::string::npos) << text;
}

TEST(Unrolling, LeavesPipelinedLoopsAlone) {
  Generated generated(read_example("dot.hc"));
  ASSERT_TRUE(generated.ok()) << generated.messages();
  ASSERT_TRUE(minihls::transforms::optimize(generated.module()));
  const std::string text = generated.text();
  // A pipelined loop is scheduled, not unrolled, and the request has to
  // survive the pipeline for the scheduler in P6 to read.
  EXPECT_NE(text.find("scf.for"), std::string::npos) << text;
  EXPECT_NE(text.find("minihls.pipeline_ii = 1"), std::string::npos) << text;
}

TEST(Unrolling, HandlesAPartialFactor) {
  // Four of eight iterations per copy: the loop stays, with two iterations.
  Generated generated("u16 f(u8 a) { u16 s = 0; for (u4 i = 0; i < 8; i = i + 1) "
                      "{ #pragma unroll factor=4\n s = s + a; } return s; }");
  ASSERT_TRUE(generated.ok()) << generated.messages();
  EXPECT_EQ(optimize_and_check(generated), "") << generated.text();
}

TEST(Metrics, IgnoresWhatCostsNoHardware) {
  // Constants and width conversions are wires, not operators.
  Generated generated("u16 f(u8 a) { return a + 1; }");
  ASSERT_TRUE(generated.ok()) << generated.messages();
  const auto metrics = minihls::transforms::measure(generated.module());
  EXPECT_EQ(metrics.operator_count, 1u);
  EXPECT_EQ(metrics.operators.at("arith.addi"), 1u);
  EXPECT_EQ(metrics.operators.count("arith.constant"), 0u);
  EXPECT_EQ(metrics.operators.count("arith.extui"), 0u);
}

}  // namespace
