#include "frontend/lexer.hpp"

#include "support/bits.hpp"

#include <cstdint>
#include <limits>

namespace minihls {
namespace {

bool is_identifier_start(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

bool is_identifier_continue(char c) { return is_identifier_start(c) || is_digit(c); }

// Numeric value of a digit in any supported base, or -1 if not a digit.
int digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

TokenKind keyword_kind(std::string_view text) {
  if (text == "if") return TokenKind::KwIf;
  if (text == "else") return TokenKind::KwElse;
  if (text == "for") return TokenKind::KwFor;
  if (text == "return") return TokenKind::KwReturn;
  if (text == "const") return TokenKind::KwConst;
  if (text == "stream") return TokenKind::KwStream;
  if (text == "pragma") return TokenKind::KwPragma;
  if (text == "void") return TokenKind::KwVoid;
  return TokenKind::Identifier;
}

}  // namespace

Lexer::Lexer(const SourceFile& file, DiagnosticEngine& diagnostics)
    : file_(&file), diagnostics_(&diagnostics), source_(file.contents()) {}

char Lexer::peek(std::size_t ahead) const {
  const std::size_t index = position_ + ahead;
  return index < source_.size() ? source_[index] : '\0';
}

bool Lexer::at_end() const { return position_ >= source_.size(); }

char Lexer::advance() { return source_[position_++]; }

bool Lexer::match(char expected) {
  if (peek() != expected) return false;
  ++position_;
  return true;
}

SourceLocation Lexer::here() const {
  return SourceLocation{static_cast<std::uint32_t>(position_)};
}

SourceRange Lexer::range_from(SourceLocation begin) const {
  return SourceRange{begin, here()};
}

void Lexer::skip_trivia() {
  while (!at_end()) {
    const char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
    } else if (c == '/' && peek(1) == '/') {
      while (!at_end() && peek() != '\n') advance();
    } else if (c == '/' && peek(1) == '*') {
      const SourceLocation begin = here();
      advance();
      advance();
      bool closed = false;
      while (!at_end()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed) {
        diagnostics_->error(range_from(begin), "unterminated block comment");
      }
    } else {
      return;
    }
  }
}

bool Lexer::classify_int_type(std::string_view text, unsigned& width, bool& is_signed) {
  if (text.size() < 2) return false;
  if (text[0] != 'i' && text[0] != 'u') return false;
  for (std::size_t i = 1; i < text.size(); ++i) {
    if (!is_digit(text[i])) return false;
  }

  is_signed = text[0] == 'i';
  unsigned parsed = 0;
  for (std::size_t i = 1; i < text.size(); ++i) {
    // Saturate rather than wrap; the caller reports anything out of range.
    if (parsed > (std::numeric_limits<unsigned>::max() - 9) / 10) {
      parsed = std::numeric_limits<unsigned>::max();
      break;
    }
    parsed = parsed * 10 + static_cast<unsigned>(text[i] - '0');
  }
  width = parsed;
  return true;
}

Token Lexer::lex_identifier_or_keyword() {
  const SourceLocation begin = here();
  while (!at_end() && is_identifier_continue(peek())) advance();

  Token token;
  token.range = range_from(begin);
  token.text = source_.substr(begin.offset, token.range.end.offset - begin.offset);

  unsigned width = 0;
  bool is_signed = false;
  if (classify_int_type(token.text, width, is_signed)) {
    if (width < 1 || width > kMaxWidth) {
      diagnostics_->error(token.range, "bit width must be between 1 and " +
                                           std::to_string(kMaxWidth) + ", got '" +
                                           std::string(token.text) + "'");
      // Keep going with a usable width so the parser can continue.
      width = width < 1 ? 1 : kMaxWidth;
    }
    token.kind = TokenKind::IntType;
    token.width = width;
    token.is_signed = is_signed;
    return token;
  }

  token.kind = keyword_kind(token.text);
  return token;
}

Token Lexer::lex_number() {
  const SourceLocation begin = here();

  unsigned base = 10;
  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
    advance();
    advance();
    base = 16;
  } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
    advance();
    advance();
    base = 2;
  }

  std::uint64_t value = 0;
  bool any_digits = false;
  bool overflowed = false;
  while (!at_end()) {
    const char c = peek();
    if (c == '_') {  // digit separator
      advance();
      continue;
    }
    const int digit = digit_value(c);
    if (digit < 0 || static_cast<unsigned>(digit) >= base) break;
    advance();
    any_digits = true;
    const auto digit_value_u = static_cast<std::uint64_t>(digit);
    if (value > (std::numeric_limits<std::uint64_t>::max() - digit_value_u) / base) {
      overflowed = true;
    } else {
      value = value * base + digit_value_u;
    }
  }

  Token token;
  token.kind = TokenKind::IntLiteral;
  token.range = range_from(begin);
  token.value = value;

  if (!any_digits) {
    diagnostics_->error(token.range, "integer literal has no digits");
    return token;
  }
  if (overflowed) {
    diagnostics_->error(token.range,
                        "integer literal does not fit in 64 bits");
    return token;
  }
  // A trailing identifier character means something like `123abc` or `0b12`;
  // silently splitting that into two tokens would be worse than rejecting it.
  if (!at_end() && is_identifier_continue(peek())) {
    const SourceLocation bad = here();
    while (!at_end() && is_identifier_continue(peek())) advance();
    token.range = range_from(begin);
    diagnostics_->error(SourceRange{bad, here()},
                        "invalid digit in integer literal");
  }
  return token;
}

Token Lexer::lex_token() {
  const SourceLocation begin = here();
  const char c = advance();

  Token token;
  auto simple = [&](TokenKind kind) {
    token.kind = kind;
    token.range = range_from(begin);
    return token;
  };

  switch (c) {
    case '(':
      return simple(TokenKind::LParen);
    case ')':
      return simple(TokenKind::RParen);
    case '{':
      return simple(TokenKind::LBrace);
    case '}':
      return simple(TokenKind::RBrace);
    case '[':
      return simple(TokenKind::LBracket);
    case ']':
      return simple(TokenKind::RBracket);
    case ';':
      return simple(TokenKind::Semicolon);
    case ',':
      return simple(TokenKind::Comma);
    case '?':
      return simple(TokenKind::Question);
    case ':':
      return simple(TokenKind::Colon);
    case '#':
      return simple(TokenKind::Hash);
    case '+':
      return simple(TokenKind::Plus);
    case '-':
      return simple(TokenKind::Minus);
    case '*':
      return simple(TokenKind::Star);
    case '/':
      return simple(TokenKind::Slash);
    case '%':
      return simple(TokenKind::Percent);
    case '^':
      return simple(TokenKind::Caret);
    case '~':
      return simple(TokenKind::Tilde);
    case '=':
      return simple(match('=') ? TokenKind::EqualEqual : TokenKind::Assign);
    case '!':
      return simple(match('=') ? TokenKind::BangEqual : TokenKind::Bang);
    case '<':
      if (match('<')) return simple(TokenKind::LessLess);
      return simple(match('=') ? TokenKind::LessEqual : TokenKind::Less);
    case '>':
      if (match('>')) return simple(TokenKind::GreaterGreater);
      return simple(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater);
    case '&':
      return simple(match('&') ? TokenKind::AmpAmp : TokenKind::Amp);
    case '|':
      return simple(match('|') ? TokenKind::PipePipe : TokenKind::Pipe);
    default:
      break;
  }

  token.kind = TokenKind::Invalid;
  token.range = range_from(begin);
  diagnostics_->error(token.range,
                      std::string("unexpected character '") + c + "' in source");
  return token;
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  while (true) {
    skip_trivia();
    if (at_end()) break;
    const char c = peek();
    if (is_identifier_start(c)) {
      tokens.push_back(lex_identifier_or_keyword());
    } else if (is_digit(c)) {
      tokens.push_back(lex_number());
    } else {
      tokens.push_back(lex_token());
    }
  }

  Token eof;
  eof.kind = TokenKind::EndOfFile;
  eof.range = SourceRange{here(), here()};
  tokens.push_back(eof);
  return tokens;
}

}  // namespace minihls
