#pragma once

#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"
#include "frontend/source.hpp"
#include "frontend/token.hpp"

#include <memory>
#include <vector>

namespace minihls {

// Recursive-descent parser for statements and declarations, with a Pratt
// (precedence-climbing) expression parser.
//
// Errors are recovered at statement and function boundaries, so one run reports
// several independent mistakes rather than stopping at the first. A returned
// Program may therefore be incomplete; always check
// DiagnosticEngine::has_errors() before using it.
class Parser {
 public:
  Parser(const SourceFile& file, std::vector<Token> tokens, DiagnosticEngine& diagnostics);

  std::unique_ptr<ast::Program> parse_program();

 private:
  // Thrown internally to unwind to the nearest recovery point. Never escapes
  // parse_program().
  struct ParseError {};

  const Token& peek(std::size_t ahead = 0) const;
  bool check(TokenKind kind) const;
  bool check_ahead(std::size_t ahead, TokenKind kind) const;
  bool at_end() const;
  const Token& advance();
  bool match(TokenKind kind);
  const Token& expect(TokenKind kind, std::string_view context);

  // Reports an error and unwinds.
  [[noreturn]] void fail(SourceRange range, std::string message);
  [[noreturn]] void fail_here(std::string message);

  // A quoted spelling for identifiers, keywords and literals; a category name
  // for everything else.
  std::string describe_token(const Token& token) const;

  void synchronize_to_statement();
  void synchronize_to_function();

  // Declarations
  std::unique_ptr<ast::Function> parse_function();
  ast::TypePtr parse_return_type();
  ast::TypePtr parse_int_type();
  ast::Param parse_param();

  // Statements
  ast::StmtPtr parse_block();
  ast::StmtPtr parse_statement();
  ast::StmtPtr parse_var_decl(bool consume_semicolon);
  ast::StmtPtr parse_assignment(bool consume_semicolon);
  ast::StmtPtr parse_call_statement();
  ast::StmtPtr parse_if();
  ast::StmtPtr parse_for();
  ast::StmtPtr parse_return();
  ast::StmtPtr parse_pragma();

  // Expressions
  ast::ExprPtr parse_expression();
  ast::ExprPtr parse_conditional();
  ast::ExprPtr parse_binary(int min_precedence);
  ast::ExprPtr parse_unary();
  ast::ExprPtr parse_postfix();
  ast::ExprPtr parse_primary();
  ast::ExprPtr parse_lvalue();

  // Parses `name=<integer>` where `name` must spell `expected_name`, and returns
  // the integer literal token so the caller has both its value and its range.
  // Used by pragma parsing.
  const Token& parse_named_integer(std::string_view expected_name,
                                   std::string_view context);

  const SourceFile* file_;
  std::vector<Token> tokens_;
  DiagnosticEngine* diagnostics_;
  std::size_t position_ = 0;
};

// Lexes and parses a source file in one step.
std::unique_ptr<ast::Program> parse(const SourceFile& file, DiagnosticEngine& diagnostics);

}  // namespace minihls
