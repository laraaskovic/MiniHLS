#pragma once
#include "frontend/diagnostics.hpp"
#include "frontend/token.hpp"

namespace minihls {

class Lexer {
public:
  Lexer(const SourceFile& src, Diagnostics& diags) : src_(src), diags_(diags) {}
  Token next();
  std::vector<Token> tokenize();   // until Eof, inclusive
private:
  const SourceFile& src_;
  Diagnostics& diags_;
  uint32_t pos_ = 0;

  void skipTrivia();
  Token lexWord();
  Token lexNumber();
  Token lexPragma();
  Token lexOperator();
};

} // namespace minihls