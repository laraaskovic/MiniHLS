#include "frontend/parser.hpp"

#include "frontend/ast.hpp"
#include "frontend/ast_printer.hpp"
#include "frontend/diagnostics.hpp"
#include "frontend/source.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace {

using minihls::DiagnosticEngine;
using minihls::SourceFile;
namespace ast = minihls::ast;

// Keeps the file alive for as long as the diagnostics and AST that borrow from
// it. Members are destroyed in reverse declaration order, so the file outlives
// both.
class Parsed {
 public:
  explicit Parsed(std::string text)
      : file_(std::make_unique<SourceFile>("test.hc", std::move(text))),
        diagnostics_(std::make_unique<DiagnosticEngine>(*file_)),
        program_(minihls::parse(*file_, *diagnostics_)) {}

  const ast::Program& program() const { return *program_; }
  const DiagnosticEngine& diagnostics() const { return *diagnostics_; }
  bool ok() const { return !diagnostics_->has_errors(); }
  std::string printed() const { return ast::print(*program_); }

 private:
  std::unique_ptr<SourceFile> file_;
  std::unique_ptr<DiagnosticEngine> diagnostics_;
  std::unique_ptr<ast::Program> program_;
};

// Parses a bare expression by wrapping it in a function, then prints it back.
// Comparing the printed form is a compact way to assert on the tree's shape:
// the printer only emits parentheses where precedence requires them, so the
// output reveals exactly how the expression was grouped.
std::string regroup(const std::string& expression) {
  const Parsed parsed("i32 f() { return " + expression + "; }");
  if (!parsed.ok()) return "<error: " + parsed.diagnostics().format_all() + ">";
  const ast::Function& function = *parsed.program().functions.front();
  const ast::Stmt& body = *function.body;
  if (body.statements.empty()) return "<no statements>";
  const ast::Stmt& ret = *body.statements.front();
  if (ret.value == nullptr) return "<no expression>";
  return ast::print(*ret.value);
}

// -------------------------------------------------------------------------
// Precedence and associativity
// -------------------------------------------------------------------------

TEST(ParserExpressions, AppliesMultiplicativeBeforeAdditive) {
  EXPECT_EQ(regroup("a + b * c"), "a + b * c");
  EXPECT_EQ(regroup("a * b + c"), "a * b + c");
  // The parentheses are load-bearing here, so the printer must keep them.
  EXPECT_EQ(regroup("(a + b) * c"), "(a + b) * c");
  EXPECT_EQ(regroup("a * (b + c)"), "a * (b + c)");
}

TEST(ParserExpressions, BinaryOperatorsAreLeftAssociative) {
  // `a - b - c` means `(a - b) - c`, which prints without parentheses.
  EXPECT_EQ(regroup("a - b - c"), "a - b - c");
  // `a - (b - c)` is a different tree and must keep its parentheses.
  EXPECT_EQ(regroup("a - (b - c)"), "a - (b - c)");
  EXPECT_EQ(regroup("a / b / c"), "a / b / c");
  EXPECT_EQ(regroup("a / (b / c)"), "a / (b / c)");
}

TEST(ParserExpressions, FollowsTheDocumentedPrecedenceLadder) {
  // Loosest to tightest: || && | ^ & ==/!= relational shift +- */%
  EXPECT_EQ(regroup("a || b && c"), "a || b && c");
  EXPECT_EQ(regroup("a && b | c"), "a && b | c");
  EXPECT_EQ(regroup("a | b ^ c"), "a | b ^ c");
  EXPECT_EQ(regroup("a ^ b & c"), "a ^ b & c");
  EXPECT_EQ(regroup("a & b == c"), "a & b == c");
  EXPECT_EQ(regroup("a == b < c"), "a == b < c");
  EXPECT_EQ(regroup("a < b << c"), "a < b << c");
  EXPECT_EQ(regroup("a << b + c"), "a << b + c");
  EXPECT_EQ(regroup("a + b * c"), "a + b * c");

  // And the reverse groupings all need parentheses.
  EXPECT_EQ(regroup("(a || b) && c"), "(a || b) && c");
  EXPECT_EQ(regroup("(a & b) == c"), "(a & b) == c");
  EXPECT_EQ(regroup("(a << b) + c"), "(a << b) + c");
}

TEST(ParserExpressions, BindsUnaryTighterThanBinary) {
  EXPECT_EQ(regroup("-a + b"), "-a + b");
  EXPECT_EQ(regroup("-(a + b)"), "-(a + b)");
  EXPECT_EQ(regroup("~a & b"), "~a & b");
  EXPECT_EQ(regroup("!a || b"), "!a || b");
  // Nested unary operators are parenthesized so the output cannot re-lex as a
  // different (or, in a future language version, nonexistent) operator.
  EXPECT_EQ(regroup("- -a"), "-(-a)");
}

TEST(ParserExpressions, BindsPostfixTightestOfAll) {
  EXPECT_EQ(regroup("a[i] + b[j]"), "a[i] + b[j]");
  EXPECT_EQ(regroup("-a[i]"), "-a[i]");
  EXPECT_EQ(regroup("a[i + 1]"), "a[i + 1]");
  EXPECT_EQ(regroup("a[b[i]]"), "a[b[i]]");
  EXPECT_EQ(regroup("read(s) + 1"), "read(s) + 1");
}

TEST(ParserExpressions, ConditionalIsLoosestAndRightAssociative) {
  EXPECT_EQ(regroup("a ? b : c"), "a ? b : c");
  EXPECT_EQ(regroup("a + 1 ? b : c"), "a + 1 ? b : c");
  // Right-associative: the second `?:` is the else-arm of the first, so no
  // parentheses are needed.
  EXPECT_EQ(regroup("a ? b : c ? d : e"), "a ? b : c ? d : e");
  // Grouping it the other way is a different tree.
  EXPECT_EQ(regroup("(a ? b : c) ? d : e"), "(a ? b : c) ? d : e");
}

// -------------------------------------------------------------------------
// Declarations and statements
// -------------------------------------------------------------------------

TEST(ParserDeclarations, ParsesEveryParameterKind) {
  const Parsed parsed(
      "void f(i32 scalar, i16 memory[64], const i8 rom[4], stream<u8> port) { return; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();

  const ast::Function& function = *parsed.program().functions.front();
  ASSERT_EQ(function.params.size(), 4u);

  EXPECT_EQ(function.params[0].type->kind, ast::TypeKind::Int);
  EXPECT_EQ(function.params[0].type->width, 32u);

  EXPECT_EQ(function.params[1].type->kind, ast::TypeKind::Array);
  EXPECT_EQ(function.params[1].type->array_size, 64u);
  EXPECT_FALSE(function.params[1].type->is_const);

  EXPECT_EQ(function.params[2].type->kind, ast::TypeKind::Array);
  EXPECT_TRUE(function.params[2].type->is_const);

  EXPECT_EQ(function.params[3].type->kind, ast::TypeKind::Stream);
  EXPECT_EQ(function.params[3].type->width, 8u);
}

TEST(ParserDeclarations, ParsesMultipleFunctions) {
  const Parsed parsed("i8 a() { return 1; } i8 b() { return 2; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();
  EXPECT_EQ(parsed.program().functions.size(), 2u);
}

TEST(ParserStatements, ParsesArrayInitializerLists) {
  const Parsed parsed("i8 f() { const i8 c[3] = { 1, -2, 3 }; return c[0]; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();
  const ast::Stmt& decl = *parsed.program().functions.front()->body->statements.front();
  EXPECT_TRUE(decl.init_is_list);
  EXPECT_EQ(decl.init.size(), 3u);
}

TEST(ParserStatements, AcceptsATrailingCommaInAnInitializer) {
  const Parsed parsed("i8 f() { const i8 c[2] = { 1, 2, }; return c[0]; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();
  EXPECT_EQ(parsed.program().functions.front()->body->statements.front()->init.size(), 2u);
}

TEST(ParserStatements, BindsElseToTheNearestIf) {
  // The `else` belongs to the inner `if`, so printing must not re-attach it.
  const Parsed parsed("i8 f(i8 p, i8 q) { if (p) if (q) return 1; else return 2; return 3; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();

  const ast::Stmt& outer = *parsed.program().functions.front()->body->statements.front();
  ASSERT_EQ(outer.kind, ast::StmtKind::If);
  EXPECT_EQ(outer.else_branch, nullptr);
  ASSERT_NE(outer.then_branch, nullptr);
  EXPECT_EQ(outer.then_branch->kind, ast::StmtKind::If);
  EXPECT_NE(outer.then_branch->else_branch, nullptr);
}

TEST(ParserStatements, ParsesForLoopClauses) {
  const Parsed parsed("i8 f() { for (u4 i = 0; i < 8; i = i + 1) { } return 0; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();

  const ast::Stmt& loop = *parsed.program().functions.front()->body->statements.front();
  ASSERT_EQ(loop.kind, ast::StmtKind::For);
  ASSERT_NE(loop.for_init, nullptr);
  EXPECT_EQ(loop.for_init->kind, ast::StmtKind::VarDecl);
  ASSERT_NE(loop.for_step, nullptr);
  EXPECT_EQ(loop.for_step->kind, ast::StmtKind::Assign);
  EXPECT_NE(loop.value, nullptr);
}

TEST(ParserStatements, DistinguishesCallStatementsFromAssignments) {
  const Parsed parsed("void f(stream<u8> s) { u8 x = read(s); write(s, x); x = 1; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();

  const auto& statements = parsed.program().functions.front()->body->statements;
  ASSERT_EQ(statements.size(), 3u);
  EXPECT_EQ(statements[0]->kind, ast::StmtKind::VarDecl);
  EXPECT_EQ(statements[1]->kind, ast::StmtKind::Call);
  EXPECT_EQ(statements[2]->kind, ast::StmtKind::Assign);
}

TEST(ParserStatements, ParsesBareReturn) {
  const Parsed parsed("void f() { return; }");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();
  EXPECT_EQ(parsed.program().functions.front()->body->statements.front()->value, nullptr);
}

// -------------------------------------------------------------------------
// Pragmas
// -------------------------------------------------------------------------

TEST(ParserPragmas, ParsesEveryPragmaForm) {
  const Parsed parsed(R"(
    i8 f() {
      #pragma pipeline II=2
      #pragma unroll
      #pragma unroll factor=4
      #pragma partition buffer
      #pragma partition buffer factor=2
      return 0;
    }
  )");
  ASSERT_TRUE(parsed.ok()) << parsed.diagnostics().format_all();

  const auto& statements = parsed.program().functions.front()->body->statements;
  ASSERT_EQ(statements.size(), 6u);

  EXPECT_EQ(statements[0]->pragma.kind, ast::PragmaKind::Pipeline);
  EXPECT_EQ(statements[0]->pragma.initiation_interval, 2u);

  EXPECT_EQ(statements[1]->pragma.kind, ast::PragmaKind::Unroll);
  EXPECT_EQ(statements[1]->pragma.factor, 0u);  // zero means completely

  EXPECT_EQ(statements[2]->pragma.factor, 4u);

  EXPECT_EQ(statements[3]->pragma.kind, ast::PragmaKind::Partition);
  EXPECT_EQ(statements[3]->pragma.array_name, "buffer");
  EXPECT_EQ(statements[3]->pragma.factor, 0u);

  EXPECT_EQ(statements[4]->pragma.factor, 2u);
}

// A typo in a pragma that silently did nothing would be worse than a build
// failure, so unknown pragmas are errors.
TEST(ParserPragmas, RejectsUnknownPragmas) {
  const Parsed parsed("i8 f() { #pragma pipelien II=1\n return 0; }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("unknown pragma"));
}

TEST(ParserPragmas, RejectsAZeroInitiationInterval) {
  const Parsed parsed("i8 f() { #pragma pipeline II=0\n return 0; }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("at least 1"));
}

// -------------------------------------------------------------------------
// Diagnostics
// -------------------------------------------------------------------------

TEST(ParserDiagnostics, NamesTheExpectedAndFoundTokens) {
  const Parsed parsed("i32 f() { return 1 }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("expected ';'"));
  EXPECT_TRUE(parsed.diagnostics().contains("but found '}'"));
}

// A missing semicolon is anchored just past the last token of the statement,
// not at the unrelated token that happened to follow on the next line.
TEST(ParserDiagnostics, ReportsAMissingSemicolonAtTheEndOfTheStatement) {
  const Parsed parsed("i32 f() {\n  return 1\n}\n");
  ASSERT_FALSE(parsed.ok());
  const std::string text = parsed.diagnostics().format_all();
  // `1` ends at line 2, column 11; the caret belongs there rather than at 3:1.
  EXPECT_NE(text.find("test.hc:2:11"), std::string::npos) << text;
  EXPECT_NE(text.find('^'), std::string::npos) << text;
}

TEST(ParserDiagnostics, ReportsOtherErrorsAtTheOffendingToken) {
  const Parsed parsed("i32 f() {\n  return * 1;\n}\n");
  ASSERT_FALSE(parsed.ok());
  const std::string text = parsed.diagnostics().format_all();
  EXPECT_NE(text.find("test.hc:2:10"), std::string::npos) << text;
  EXPECT_TRUE(parsed.diagnostics().contains("expected an expression"));
}

// Recovering at statement boundaries is what lets one run surface several
// independent mistakes instead of stopping at the first.
TEST(ParserDiagnostics, RecoversToReportMultipleErrors) {
  const Parsed parsed(R"(
    i32 f() {
      i32 a = ;
      i32 b = 2;
      i32 c = ;
      return b;
    }
  )");
  EXPECT_FALSE(parsed.ok());
  EXPECT_GE(parsed.diagnostics().error_count(), 2u);
}

TEST(ParserDiagnostics, RecoversAcrossFunctionsToKeepParsing) {
  const Parsed parsed("i32 broken( { return 0; } i32 fine() { return 1; }");
  EXPECT_FALSE(parsed.ok());
  // The second function is still recognized despite the first being malformed.
  bool found_fine = false;
  for (const auto& function : parsed.program().functions) {
    if (function->name == "fine") found_fine = true;
  }
  EXPECT_TRUE(found_fine);
}

TEST(ParserDiagnostics, RejectsConstOnNonArrays) {
  const Parsed parsed("i8 f() { const i8 x = 1; return x; }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("'const' applies only to arrays"));
}

TEST(ParserDiagnostics, RequiresAnInitializerOnConstArrays) {
  const Parsed parsed("i8 f() { const i8 c[4]; return c[0]; }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("must have an initializer"));
}

TEST(ParserDiagnostics, RejectsZeroLengthArrays) {
  const Parsed parsed("i8 f(i8 a[0]) { return a[0]; }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("at least 1"));
}

TEST(ParserDiagnostics, RejectsAStraySemicolon) {
  const Parsed parsed("i8 f() { ; return 0; }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("stray ';'"));
}

TEST(ParserDiagnostics, RejectsScalarInitializerOnArrays) {
  const Parsed parsed("i8 f() { i8 a[4] = 1; return a[0]; }");
  EXPECT_FALSE(parsed.ok());
  EXPECT_TRUE(parsed.diagnostics().contains("braced list"));
}

// Malformed input must never hang the parser; recovery always consumes input.
TEST(ParserDiagnostics, TerminatesOnPathologicalInput) {
  for (const std::string text : {"{{{{", "i32", "i32 f(", "i32 f() {", "))))", "#pragma",
                                 "i32 f() { for (", "i32 f() { if ("}) {
    const Parsed parsed(text);
    EXPECT_FALSE(parsed.ok()) << "expected an error for: " << text;
  }
}

}  // namespace
