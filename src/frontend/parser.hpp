#pragma once
#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"

namespace minihls {

class Parser {
public:
  Parser(std::vector<Token> tokens, const SourceFile& src, Diagnostics& diags)
      : toks_(std::move(tokens)), src_(src), diags_(diags) {}

  Program parseProgram();
  ExprPtr parseExpressionOnly();            // entry point for tests

private:
  // token helpers
  const Token& peek(size_t ahead = 0) const;
  bool check(Tok k, size_t ahead = 0) const;
  bool accept(Tok k);
  Token advance();
  Token expect(Tok k, const char* what);
  [[noreturn]] void fail(Range, const std::string& message);

  // expressions
  ExprPtr parseConditional();
  ExprPtr parseExpr(int minBP = 0);
  ExprPtr parseUnary();
  ExprPtr parsePostfix();
  ExprPtr parsePrimary();

  // statements
  BlockPtr parseBlock();
  StmtPtr  parseStatement();
  StmtPtr  parseDeclaration();              // var_decl or array_decl
  StmtPtr  parseAssign();
  StmtPtr  parseWrite();
  StmtPtr  parseIf();
  StmtPtr  parseFor();
  StmtPtr  parseReturn();

  // program
  ConstDecl parseConstDecl();
  Function  parseFunction();
  Param     parseParam();
  Type      parseType(const char* what);
  Pragma    parsePragma(Token);

  std::vector<Token> toks_;
  const SourceFile& src_;
  Diagnostics& diags_;
  size_t pos_ = 0;
};

} // namespace minihls