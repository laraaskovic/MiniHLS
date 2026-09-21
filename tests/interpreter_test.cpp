#include <gtest/gtest.h>
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "interp/interpreter.hpp"
#include "sema/const_eval.hpp"
#include "sema/resolver.hpp"
#include "sema/type_check.hpp"

using namespace minihls;

namespace {

// The whole frontend pipeline, then run it.
std::optional<RunResult> runSource(const std::string& text,
                                   const std::vector<ParamValue>& args,
                                   bool* compileFailed = nullptr) {
  static std::deque<SourceFile> srcs;
  static std::deque<Diagnostics> diags;
  static std::deque<Program> progs;

  srcs.emplace_back("<test>", text);
  diags.emplace_back(srcs.back());
  auto& d = diags.back();

  auto toks = Lexer(srcs.back(), d).tokenize();
  progs.push_back(Parser(std::move(toks), srcs.back(), d).parseProgram());
  auto& p = progs.back();

  Resolver(d).resolve(p);
  TypeChecker(d).check(p);
  ConstantEvaluator(d).evaluate(p);
  if (compileFailed) *compileFailed = d.hasErrors();
  if (d.hasErrors()) return std::nullopt;

  return Interpreter(d).run(p, args);
}

ParamValue S(i128 v, unsigned w, bool sgn = true) {
  return ParamValue{Bits::make(static_cast<u128>(v), w, sgn), {}};
}
ParamValue A(std::vector<i128> vs, unsigned w, bool sgn = true) {
  ParamValue p;
  for (i128 v : vs) p.elements.push_back(Bits::make(static_cast<u128>(v), w, sgn));
  return p;
}
int64_t val(const Bits& b) { return static_cast<int64_t>(b.value()); }

} // namespace

// ---- straight-line ----

TEST(Interp, Max3) {
  const char* src = R"(
    i16 max3(i16 a, i16 b, i16 c) {
      i16 largest = a > b ? a : b;
      return largest > c ? largest : c;
    })";
  auto r = runSource(src, {S(1,16), S(2,16), S(3,16)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 3);

  r = runSource(src, {S(-5,16), S(-2,16), S(-9,16)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), -2);   // signed comparison

  r = runSource(src, {S(-32768,16), S(-32768,16), S(-32768,16)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), -32768);
}

// ---- branches and loops ----

TEST(Interp, AbsDiffTakesBothBranches) {
  const char* src = R"(
    u16 abs_diff(i16 a, i16 b) {
      i17 d = a - b;
      i18 m = 0;
      if (d < 0) { m = -d; } else { m = d; }
      return u16(m);
    })";
  auto r = runSource(src, {S(32767,16), S(-32768,16)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 65535);   // widest difference

  r = runSource(src, {S(0,16), S(-32768,16)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 32768);   // needs the i18
}

TEST(Interp, DotWithLoopCarriedAccumulator) {
  const char* src = R"(
    i32 dot(i16 x[8], i16 y[8]) {
      i32 acc = 0;
      for (u4 i = 0; i < 8; i = i + 1) { acc = i32(acc + x[i] * y[i]); }
      return acc;
    })";
  auto r = runSource(src, {A({1,2,3,4,5,6,7,8},16), A({8,7,6,5,4,3,2,1},16)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 120);
}

TEST(Interp, DotTruncatesEveryIteration) {
  // 8 * 32767^2 = 8589410312; low 32 bits as i32 = -524280.
  // Truncating after each add must equal truncating the exact total —
  // that's only true because addition mod 2^32 is closed.
  const char* src = R"(
    i32 dot(i16 x[8], i16 y[8]) {
      i32 acc = 0;
      for (u4 i = 0; i < 8; i = i + 1) { acc = i32(acc + x[i] * y[i]); }
      return acc;
    })";
  std::vector<i128> all(8, 32767);
  auto r = runSource(src, {A(all,16), A(all,16)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), -524280);
}

// ---- the runtime-behaviour table ----

TEST(Interp, DivideAndRemainderByZero) {
  auto r = runSource("i9 f(i8 a) { return a / 0; }", {S(42,8)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 0);
  r = runSource("i8 f(i8 a) { return a % 0; }", {S(42,8)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 0);
}

TEST(Interp, ShiftsAtAndBeyondTheWidth) {
  auto r = runSource("u8 f(u8 a, u8 s) { return a << s; }", {S(0xFF,8,false), S(8,8,false)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 0);

  r = runSource("i8 f(i8 a, u8 s) { return a >> s; }", {S(-1,8), S(99,8,false)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), -1);   // signed negative -> all ones
}

TEST(Interp, OutOfRangeArrayAccess) {
  // read -> 0, write -> ignored
  auto r = runSource(
      "i16 f(i16 x[4], u8 i) { return x[i]; }",
      {A({1,2,3,4},16), S(9,8,false)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 0);
}

TEST(Interp, UnwrittenArrayElementsReadAsZero) {
  auto r = runSource(
      "i16 f() { i16 buf[4]; buf[0] = 5; return buf[2]; }", {});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 0);
}

// ---- the mux-vs-branch distinction ----

TEST(Interp, TernaryEvaluatesBothArms) {
  // Nothing short-circuits: the untaken arm's division by zero is DEFINED
  // as 0, and evaluating it must not fault. In hardware both arms are wires
  // into a mux, so both are always computed.
  auto r = runSource("i9 f(i8 a) { return a > 0 ? a / 1 : a / 0; }", {S(5,8)});
  ASSERT_TRUE(r); EXPECT_EQ(val(r->returnValue), 5);
}

// ---- streams ----

TEST(Interp, StreamSumWritesRunningTotals) {
  const char* src = R"(
    i32 stream_sum(in stream<i16> s, out stream<i32> r) {
      i32 acc = 0;
      for (u4 i = 0; i < 8; i = i + 1) {
        i16 v = read(s);
        acc = i32(acc + v);
        write(r, acc);
      }
      return acc;
    })";
  ParamValue in = A({1,2,3,4,5,6,7,8},16);
  auto res = runSource(src, {in, ParamValue{}});
  ASSERT_TRUE(res);
  EXPECT_EQ(val(res->returnValue), 36);
  ASSERT_EQ(res->streamOutputs.size(), 1u);
  std::vector<int64_t> got;
  for (auto& b : res->streamOutputs[0]) got.push_back(val(b));
  EXPECT_EQ(got, (std::vector<int64_t>{1,3,6,10,15,21,28,36}));
}