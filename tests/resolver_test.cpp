#include <gtest/gtest.h>
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/resolver.hpp"
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

Parsed parse(const std::string& text) {
  Parsed result;
  result.source = std::make_unique<SourceFile>("<test>", text);
  result.diagnostics = std::make_unique<Diagnostics>(*result.source);
  auto tokens = Lexer(*result.source, *result.diagnostics).tokenize();
  Parser parser(std::move(tokens), *result.source, *result.diagnostics);
  result.program = parser.parseProgram();
  return result;
}

bool resolveHasError(const std::string& text) {
  auto parsed = parse(text);
  Resolver resolver(*parsed.diagnostics);
  resolver.resolve(parsed.program);
  return parsed.diagnostics->hasErrors();
}

std::string readFile(const std::string& path) {
  std::ifstream input(path);
  std::ostringstream text;
  text << input.rdbuf();
  return text.str();
}

} // namespace

TEST(ScopeStack, AllowsShadowingButRejectsRedeclaration) {
  ScopeStack scopes;
  scopes.push();
  Symbol outer{SymbolKind::Local, "x", Type{8, false}, {}, false, 0};
  Symbol inner{SymbolKind::Local, "x", Type{16, false}, {}, false, 0};
  EXPECT_TRUE(scopes.declare(&outer));
  EXPECT_FALSE(scopes.declare(&inner));
  scopes.push();
  EXPECT_TRUE(scopes.declare(&inner));
  EXPECT_EQ(scopes.lookup("x"), &inner);
  scopes.pop();
  EXPECT_EQ(scopes.lookup("x"), &outer);
}

TEST(Resolver, AnnotatesNamesAndResolvesConstAfterFunction) {
  auto parsed = parse(
      "i32 f(i16 x[8]) { i32 y = K; y = x[0]; return y; }"
      "const i32 K = 1;");
  ASSERT_FALSE(parsed.diagnostics->hasErrors());
  Resolver resolver(*parsed.diagnostics);
  resolver.resolve(parsed.program);
  EXPECT_FALSE(parsed.diagnostics->hasErrors());

  auto* body = parsed.program.fn.body.get();
  auto& declaration = static_cast<VarDecl&>(*body->stmts[0]);
  auto& assignment = static_cast<Assign&>(*body->stmts[1]);
  auto& index = static_cast<Index&>(*assignment.value);
  ASSERT_NE(declaration.init, nullptr);
  ASSERT_NE(static_cast<NameRef*>(declaration.init.get())->symbol, nullptr);
  ASSERT_NE(assignment.symbol, nullptr);
  ASSERT_NE(index.symbol, nullptr);
  EXPECT_EQ(index.symbol->kind, SymbolKind::ArrayParam);
}

TEST(Resolver, ReportsUnknownNamesAndSameScopeRedeclarations) {
  EXPECT_TRUE(resolveHasError("i16 f() { i16 x = missing; i16 x = 1; return x; }"));
}

TEST(Resolver, AllowsNestedShadowing) {
  EXPECT_FALSE(resolveHasError("i16 f() { i16 x = 1; if (x) { i16 x = 2; } return x; }"));
}

TEST(Resolver, RejectsInvalidAssignmentTargets) {
  EXPECT_TRUE(resolveHasError("i16 f(i16 a[2]) { a[0] = 1; return 0; }"));
  EXPECT_TRUE(resolveHasError("i16 f() { i16 x = 0; x[0] = 1; return x; }"));
  EXPECT_TRUE(resolveHasError("i16 f() { i16 a[2]; a = 1; return 0; }"));
  EXPECT_TRUE(resolveHasError("i16 f() { for (u4 i = 0; i < 2; i = i + 1) { i = 1; } return 0; }"));
  EXPECT_TRUE(resolveHasError("i16 f() { K = 1; return 0; } const i16 K = 0;"));
}

TEST(Resolver, ParameterArraySizesUseTheGlobalScope) {
  EXPECT_FALSE(resolveHasError(
      "i16 f(i16 values[N], u4 N) { return values[0]; }"
      "const u4 N = 8;"));
}

TEST(Resolver, ChecksStreamDirections) {
  EXPECT_TRUE(resolveHasError("i16 f(in stream<i16> s) { write(s, 1); return 0; }"));
  EXPECT_TRUE(resolveHasError("i16 f(out stream<i16> s) { i16 x = read(s); return x; }"));
  EXPECT_FALSE(resolveHasError("i16 f(in stream<i16> s, out stream<i16> r) { i16 x = read(s); write(r, x); return x; }"));
}

TEST(Resolver, AllExamplesResolve) {
  for (const char* name : {"max3", "abs_diff", "popcount", "dot", "fir", "stream_sum"}) {
    auto parsed = parse(readFile(std::string(MINIHLS_EXAMPLES_DIR) + "/" + name + ".hc"));
    ASSERT_FALSE(parsed.diagnostics->hasErrors()) << name << " failed to parse";
    Resolver resolver(*parsed.diagnostics);
    resolver.resolve(parsed.program);
    EXPECT_FALSE(parsed.diagnostics->hasErrors()) << name;
  }
}