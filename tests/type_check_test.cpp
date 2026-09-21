#include <gtest/gtest.h>
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
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

Parsed parseAndResolve(const std::string& text) {
  Parsed result;
  result.source = std::make_unique<SourceFile>("<test>", text);
  result.diagnostics = std::make_unique<Diagnostics>(*result.source);
  auto tokens = Lexer(*result.source, *result.diagnostics).tokenize();
  Parser parser(std::move(tokens), *result.source, *result.diagnostics);
  result.program = parser.parseProgram();
  Resolver resolver(*result.diagnostics);
  resolver.resolve(result.program);
  return result;
}

bool hasTypeError(const std::string& text) {
  auto parsed = parseAndResolve(text);
  if (parsed.diagnostics->hasErrors()) return true;
  TypeChecker checker(*parsed.diagnostics);
  checker.check(parsed.program);
  return parsed.diagnostics->hasErrors();
}

std::string readFile(const std::string& path) {
  std::ifstream input(path);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

} // namespace

TEST(TypeChecker, ArithmeticGetsWidthFromTheOperatorTable) {
  auto parsed = parseAndResolve("i17 f(i16 a, i16 b) { i17 x = a + b; return x; }");
  ASSERT_FALSE(parsed.diagnostics->hasErrors());
  TypeChecker checker(*parsed.diagnostics);
  checker.check(parsed.program);
  EXPECT_FALSE(parsed.diagnostics->hasErrors());
  auto& declaration = static_cast<VarDecl&>(*parsed.program.fn.body->stmts[0]);
  auto& expression = static_cast<Binary&>(*declaration.init);
  EXPECT_TRUE(expression.typeKnown);
  EXPECT_EQ(expression.type.width, 17u);
  EXPECT_TRUE(expression.type.isSigned);
}

TEST(TypeChecker, UntypedLiteralUsesDeclarationContext) {
  EXPECT_FALSE(hasTypeError("i32 f() { i32 x = 0; return x; }"));
  EXPECT_TRUE(hasTypeError("u8 f() { u8 x = 300; return x; }"));
}

TEST(TypeChecker, RejectsMixedSignednessAndNarrowing) {
  EXPECT_TRUE(hasTypeError("i16 f(i16 a, u16 b) { i16 x = a + b; return x; }"));
  EXPECT_TRUE(hasTypeError("i16 f(i32 x) { i16 y = x; return y; }"));
}

TEST(TypeChecker, ConditionsMustBeU1) {
  EXPECT_TRUE(hasTypeError("i16 f(i16 x) { if (x) { return 1; } return 0; }"));
}

TEST(TypeChecker, ExamplesTypeCheck) {
  EXPECT_FALSE(hasTypeError("i32 f(i16 a, i16 b) { i17 d = a - b; return i32(d); }"));
}

TEST(TypeChecker, AllExamplesTypeCheck) {
  for (const char* name : {"max3", "abs_diff", "popcount", "dot", "fir", "stream_sum"}) {
    auto text = readFile(std::string(MINIHLS_EXAMPLES_DIR) + "/" + name + ".hc");
    EXPECT_FALSE(hasTypeError(text)) << name;
  }
}