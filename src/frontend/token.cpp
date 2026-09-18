#include "frontend/token.hpp"

namespace minihls {

std::string_view describe(TokenKind kind) {
  switch (kind) {
    case TokenKind::EndOfFile:
      return "end of file";
    case TokenKind::Invalid:
      return "invalid token";
    case TokenKind::Identifier:
      return "identifier";
    case TokenKind::IntLiteral:
      return "integer literal";
    case TokenKind::IntType:
      return "type";
    case TokenKind::KwIf:
      return "'if'";
    case TokenKind::KwElse:
      return "'else'";
    case TokenKind::KwFor:
      return "'for'";
    case TokenKind::KwReturn:
      return "'return'";
    case TokenKind::KwConst:
      return "'const'";
    case TokenKind::KwStream:
      return "'stream'";
    case TokenKind::KwPragma:
      return "'pragma'";
    case TokenKind::KwVoid:
      return "'void'";
    case TokenKind::LParen:
      return "'('";
    case TokenKind::RParen:
      return "')'";
    case TokenKind::LBrace:
      return "'{'";
    case TokenKind::RBrace:
      return "'}'";
    case TokenKind::LBracket:
      return "'['";
    case TokenKind::RBracket:
      return "']'";
    case TokenKind::Semicolon:
      return "';'";
    case TokenKind::Comma:
      return "','";
    case TokenKind::Question:
      return "'?'";
    case TokenKind::Colon:
      return "':'";
    case TokenKind::Hash:
      return "'#'";
    case TokenKind::Assign:
      return "'='";
    case TokenKind::EqualEqual:
      return "'=='";
    case TokenKind::BangEqual:
      return "'!='";
    case TokenKind::Less:
      return "'<'";
    case TokenKind::LessEqual:
      return "'<='";
    case TokenKind::Greater:
      return "'>'";
    case TokenKind::GreaterEqual:
      return "'>='";
    case TokenKind::Plus:
      return "'+'";
    case TokenKind::Minus:
      return "'-'";
    case TokenKind::Star:
      return "'*'";
    case TokenKind::Slash:
      return "'/'";
    case TokenKind::Percent:
      return "'%'";
    case TokenKind::Amp:
      return "'&'";
    case TokenKind::Pipe:
      return "'|'";
    case TokenKind::Caret:
      return "'^'";
    case TokenKind::Tilde:
      return "'~'";
    case TokenKind::LessLess:
      return "'<<'";
    case TokenKind::GreaterGreater:
      return "'>>'";
    case TokenKind::AmpAmp:
      return "'&&'";
    case TokenKind::PipePipe:
      return "'||'";
    case TokenKind::Bang:
      return "'!'";
  }
  return "token";
}

}  // namespace minihls
