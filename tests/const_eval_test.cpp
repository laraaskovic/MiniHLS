#include <gtest/gtest.h>
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/const_eval.hpp"
#include "sema/resolver.hpp"
#include "sema/type_check.hpp"
#include <fstream>
#include <memory>
#include <sstream>

using namespace minihls;

namespace {

struct Parsed {
  std::unique_ptr<SourceFile> source;
  std::unique_ptr<Diagnostics> diagnostics;
  Program program;
};

Parsed prepare(const std::string& text) {
  Parsed result;
  result.source = std::make_unique<SourceFile>("<test>", text);
  result.diagnostics = std::make_unique<Diagnostics>(*result.source);
  auto tokens = Lexer(*result.source, *result.diagnostics).tokenize();
  Parser parser(std::move(tokens), *result.source, *result.diagnostics);
  result.program = parser.parseProgram();
  Resolver resolver(*result.diagnostics);
  resolver.resolve(result.program);
  TypeChecker checker(*result.diagnostics);
  checker.check(result.program);
  return result;
}

bool hasConstError(const std::string& text) {
  auto parsed = prepare(text);
  if (parsed.diagnostics->hasErrors()) return true;
  ConstantEvaluator evaluator(*parsed.diagnostics);
  evaluator.evaluate(parsed.program);
  return parsed.diagnostics->hasErrors();
}

std::string readFile(const std::string& path) {
  std::ifstream input(path);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

} // namespace

TEST(ConstantEvaluator, FoldsConstantsWithBitsSemantics) {
  auto parsed = prepare("const u8 K = 3 * 4 + 1; u8 f() { return K; }");
  ASSERT_FALSE(parsed.diagnostics->hasErrors());
  ConstantEvaluator evaluator(*parsed.diagnostics);
  evaluator.evaluate(parsed.program);
  EXPECT_FALSE(parsed.diagnostics->hasErrors());
  ASSERT_TRUE(parsed.program.consts[0].valueKnown);
  EXPECT_EQ(parsed.program.consts[0].value.raw, static_cast<u128>(13));
}

TEST(ConstantEvaluator, DetectsCyclicConstants) {
  EXPECT_TRUE(hasConstError(
      "const i16 a = b; const i16 b = a; i16 f() { return 0; }"));
}

TEST(ConstantEvaluator, ValidatesArraySizesAndConstantIndexes) {
  EXPECT_TRUE(hasConstError("i16 f() { i16 a[0]; return 0; }"));
  EXPECT_TRUE(hasConstError("i16 f() { i16 a[2]; return a[2]; }"));
  EXPECT_FALSE(hasConstError("i16 f(u4 i) { i16 a[2]; return a[i]; }"));
}

TEST(ConstantEvaluator, ComputesTripCount) {
  auto parsed = prepare(
      "i16 f() { for (u4 i = 0; i < 8; i = i + 1) { } return 0; }");
  ASSERT_FALSE(parsed.diagnostics->hasErrors());
  ConstantEvaluator evaluator(*parsed.diagnostics);
  evaluator.evaluate(parsed.program);
  EXPECT_FALSE(parsed.diagnostics->hasErrors());
  auto& loop = static_cast<For&>(*parsed.program.fn.body->stmts[0]);
  EXPECT_TRUE(loop.tripCountKnown);
  EXPECT_EQ(loop.tripCount, 8u);
}

TEST(ConstantEvaluator, RejectsLoopTerminalValueOutsideType) {
  EXPECT_TRUE(hasConstError(
      "i16 f() { for (u6 j = 31; j > 0; j = j - 2) { } return 0; }"));
}

TEST(ConstantEvaluator, AllExamplesEvaluateConstantsAndLoops) {
  for (const char* name : {"max3", "abs_diff", "popcount", "dot", "fir", "stream_sum"}) {
    auto parsed = prepare(readFile(std::string(MINIHLS_EXAMPLES_DIR) + "/" + name + ".hc"));
    ASSERT_FALSE(parsed.diagnostics->hasErrors()) << name << " failed before S3";
    ConstantEvaluator evaluator(*parsed.diagnostics);
    evaluator.evaluate(parsed.program);
    EXPECT_FALSE(parsed.diagnostics->hasErrors()) << name;
  }
}