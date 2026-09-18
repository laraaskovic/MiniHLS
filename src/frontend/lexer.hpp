#pragma once

#include "frontend/diagnostics.hpp"
#include "frontend/source.hpp"
#include "frontend/token.hpp"

#include <vector>

namespace minihls {

// Hand-written lexer. Produces the full token vector in one pass, always
// terminated by exactly one EndOfFile token.
//
// Malformed input still yields a token stream: bad characters become
// TokenKind::Invalid and an error is reported. That lets the parser keep going
// and report more than one problem per run.
class Lexer {
 public:
  Lexer(const SourceFile& file, DiagnosticEngine& diagnostics);

  std::vector<Token> tokenize();

 private:
  char peek(std::size_t ahead = 0) const;
  bool at_end() const;
  char advance();
  bool match(char expected);

  SourceLocation here() const;
  SourceRange range_from(SourceLocation begin) const;

  void skip_trivia();
  Token lex_token();
  Token lex_identifier_or_keyword();
  Token lex_number();

  // Recognizes the reserved `iN` / `uN` type spelling. Returns false for
  // ordinary identifiers, including bare `i` and names like `i32_total`.
  static bool classify_int_type(std::string_view text, unsigned& width, bool& is_signed);

  const SourceFile* file_;
  DiagnosticEngine* diagnostics_;
  std::string_view source_;
  std::size_t position_ = 0;
};

}  // namespace minihls
