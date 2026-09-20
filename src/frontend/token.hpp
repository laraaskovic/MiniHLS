#pragma once
#include "frontend/source.hpp"
#include "support/bits.hpp"

namespace minihls {

enum class Tok {
  Eof, Identifier, IntLiteral, Type, Pragma,
  KwConst, KwElse, KwFor, KwIf, KwIn, KwOut, KwRead, KwReturn, KwStream, KwWrite,
  Plus, Minus, Star, Slash, Percent, Tilde, Amp, Pipe, Caret, Shl, Shr,
  Bang, AmpAmp, PipePipe, EqEq, BangEq, Lt, Le, Gt, Ge,
  Assign, Question, Colon,
  LParen, RParen, LBrace, RBrace, LBracket, RBracket, Comma, Semi,
};

const char* tokName(Tok);   // for tests and the dump command

struct Token {
  Tok kind = Tok::Eof;
  Range range;
  std::string_view text;    // the exact lexeme
  u128 intValue = 0;        // IntLiteral only
  unsigned width = 0;       // Type only
  bool isSigned = false;    // Type only
};

} // namespace minihls