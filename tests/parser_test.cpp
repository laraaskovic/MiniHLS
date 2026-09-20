#include <gtest/gtest.h>
#include "frontend/ast_printer.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include <fstream>
#include <sstream>

using namespace minihls;

namespace {

struct Parsed {
  std::unique_ptr<SourceFile> src;
  std::unique_ptr<Diagnostics> diags;
  std::unique_ptr<Parser> parser;
};

Parsed prepare(const std::string& text) {
  Parsed p;
  p.src = std::make_unique<SourceFile>("<test>", text);
  p.diags = std::make_unique<Diagnostics>(*p.src);
  auto toks = Lexer(*p.src, *p.diags).tokenize();
  p.parser = std::make_unique<Parser>(std::move(toks), *p.src, *p.diags);
  return p;
}

// Parse one expression and print it fully parenthesised.
std::string expr(const std::string& text) {
  auto p = prepare(text);
  ExprPtr e = p.parser->parseExpressionOnly();
  return e ? printExpr(*e) : "<null>";
}

bool programHasError(const std::string& text) {
  auto p = prepare(text);
  p.parser->parseProgram();
  return p.diags->hasErrors();
}

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}

} // namespace

// ===================== precedence: do these first ====================

TEST(Parser, MultiplyBindsTighterThanAdd) {
  EXPECT_EQ(expr("a + b * c"), "(a + (b * c))");
  EXPECT_EQ(expr("a * b + c"), "((a * b) + c)");
}

TEST(Parser, ArithmeticIsLeftAssociative) {
  EXPECT_EQ(expr("a - b - c"), "((a - b) - c)");     // NOT (a - (b - c))
  EXPECT_EQ(expr("a / b / c"), "((a / b) / c)");
}

TEST(Parser, ShiftsBindLikeMultiplication) {
  // LANGUAGE.md: `a + b << 2` is `a + (b << 2)` — this is the Go table,
  // and it differs from C.
  EXPECT_EQ(expr("a + b << 2"), "(a + (b << 2))");
}

TEST(Parser, BitwiseAndBindsTighterThanEquality) {
  // The whole reason for choosing Go's table: in C this parses as
  // `x & (mask == 0)`.
  EXPECT_EQ(expr("x & mask == 0"), "((x & mask) == 0)");
}

TEST(Parser, LogicalOrIsLoosest) {
  EXPECT_EQ(expr("a && b || c && d"), "((a && b) || (c && d))");
  EXPECT_EQ(expr("a < b && c < d"),   "((a < b) && (c < d))");
}

TEST(Parser, TernaryIsRightAssociative) {
  EXPECT_EQ(expr("a ? b : c ? d : e"), "(a ? b : (c ? d : e))");
}

TEST(Parser, TernaryIsLooserThanEverythingElse) {
  EXPECT_EQ(expr("a > b ? a : b"), "((a > b) ? a : b)");
}

TEST(Parser, UnaryBindsTighterThanBinary) {
  EXPECT_EQ(expr("-a + b"), "((-a) + b)");
  EXPECT_EQ(expr("-a * b"), "((-a) * b)");
  EXPECT_EQ(expr("--a"),    "(-(-a))");          // right-associative
}

TEST(Parser, ParenthesesOverridePrecedence) {
  EXPECT_EQ(expr("(a + b) * c"), "((a + b) * c)");
}

TEST(Parser, CastsAndIndexing) {
  EXPECT_EQ(expr("i32(a + b)"), "i32((a + b))");
  EXPECT_EQ(expr("x[i] * y[i]"), "((x[i]) * (y[i]))");
  EXPECT_EQ(expr("i32(acc + x[i] * y[i])"), "i32((acc + ((x[i]) * (y[i]))))");
}

// ========================= structure rejected ========================

TEST(Parser, ForHeaderMustMatchTheRestrictedShape) {
  auto bad = [](const char* header) {
    return programHasError(std::string("i16 f() { ") + header + " { } return 0; }");
  };
  EXPECT_TRUE(bad("for (i = 0; i < 8; i = i + 1)"));        // no type in init
  EXPECT_TRUE(bad("for (u4 i = 0; i < n; i = i + 1)"));     // limit not constant… (E3)
  EXPECT_TRUE(bad("for (u4 i = 0; i < 8; i = i * 2)"));     // step must be + or -
  EXPECT_TRUE(bad("for (u4 i = 0; i != 8; i = i + 1)"));    // relOp must be < <= > >=
}

TEST(Parser, ReadIsNotAnExpression) {
  // legal only as a whole initialiser or whole RHS
  EXPECT_TRUE(programHasError(
      "i16 f(in stream<i16> s) { i16 v = read(s) + 1; return v; }"));
}

TEST(Parser, ScalarDeclarationsNeedAnInitialiser) {
  EXPECT_TRUE(programHasError("i16 f() { i16 x; return x; }"));
}

TEST(Parser, ExactlyOneFunction) {
  EXPECT_TRUE(programHasError("const i16 k = 1;"));                        // none
  EXPECT_TRUE(programHasError("i16 f() { return 0; } i16 g() { return 0; }")); // two
}

// ====================== the round-trip property ======================

TEST(Parser, ExamplesRoundTrip) {
  for (const char* name : {"max3", "abs_diff", "popcount", "dot", "fir", "stream_sum"}) {
    std::string path = std::string(MINIHLS_EXAMPLES_DIR) + "/" + name + ".hc";
    std::string text = readFile(path);
    ASSERT_FALSE(text.empty()) << path;

    auto first = prepare(text);
    std::string once = printProgram(first.parser->parseProgram());
    EXPECT_FALSE(first.diags->hasErrors()) << name;

    auto second = prepare(once);
    std::string twice = printProgram(second.parser->parseProgram());
    EXPECT_FALSE(second.diags->hasErrors()) << name << " (reparse)";

    EXPECT_EQ(once, twice) << name;   // differ => the AST lost information
  }
}