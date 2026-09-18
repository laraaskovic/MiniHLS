#pragma once

#include "frontend/source.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace minihls {

enum class TokenKind {
  EndOfFile,
  Invalid,

  Identifier,
  IntLiteral,
  // An `iN` or `uN` type token. Width and signedness are carried on the token
  // so the parser never re-parses the spelling.
  IntType,

  KwIf,
  KwElse,
  KwFor,
  KwReturn,
  KwConst,
  KwStream,
  KwPragma,
  KwVoid,

  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Semicolon,
  Comma,
  Question,
  Colon,
  Hash,

  Assign,
  EqualEqual,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,

  Plus,
  Minus,
  Star,
  Slash,
  Percent,

  Amp,
  Pipe,
  Caret,
  Tilde,
  LessLess,
  GreaterGreater,

  AmpAmp,
  PipePipe,
  Bang,
};

// A human-readable name for diagnostics: the spelling for punctuation and
// keywords ("';'", "'if'"), a category for the rest ("identifier").
std::string_view describe(TokenKind kind);

struct Token {
  TokenKind kind = TokenKind::EndOfFile;
  SourceRange range;

  // Identifiers and keywords: the spelling. Unused otherwise.
  std::string_view text;

  // IntLiteral: the parsed value.
  std::uint64_t value = 0;

  // IntType: the declared width and signedness.
  unsigned width = 0;
  bool is_signed = false;

  bool is(TokenKind k) const { return kind == k; }
};

}  // namespace minihls
