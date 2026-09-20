#include "frontend/token.hpp"

namespace minihls {

const char* tokName(Tok tok) {
  switch (tok) {
    case Tok::Eof: return "eof";
    case Tok::Identifier: return "identifier";
    case Tok::IntLiteral: return "integer literal";
    case Tok::Type: return "type";
    case Tok::Pragma: return "pragma";
    case Tok::KwConst: return "const";
    case Tok::KwElse: return "else";
    case Tok::KwFor: return "for";
    case Tok::KwIf: return "if";
    case Tok::KwIn: return "in";
    case Tok::KwOut: return "out";
    case Tok::KwRead: return "read";
    case Tok::KwReturn: return "return";
    case Tok::KwStream: return "stream";
    case Tok::KwWrite: return "write";
    case Tok::Plus: return "+"; case Tok::Minus: return "-";
    case Tok::Star: return "*"; case Tok::Slash: return "/";
    case Tok::Percent: return "%"; case Tok::Tilde: return "~";
    case Tok::Amp: return "&"; case Tok::Pipe: return "|";
    case Tok::Caret: return "^"; case Tok::Shl: return "<<";
    case Tok::Shr: return ">>"; case Tok::Bang: return "!";
    case Tok::AmpAmp: return "&&"; case Tok::PipePipe: return "||";
    case Tok::EqEq: return "=="; case Tok::BangEq: return "!=";
    case Tok::Lt: return "<"; case Tok::Le: return "<=";
    case Tok::Gt: return ">"; case Tok::Ge: return ">=";
    case Tok::Assign: return "="; case Tok::Question: return "?";
    case Tok::Colon: return ":"; case Tok::LParen: return "(";
    case Tok::RParen: return ")"; case Tok::LBrace: return "{";
    case Tok::RBrace: return "}"; case Tok::LBracket: return "[";
    case Tok::RBracket: return "]"; case Tok::Comma: return ",";
    case Tok::Semi: return ";";
  }
  return "unknown";
}

} // namespace minihls