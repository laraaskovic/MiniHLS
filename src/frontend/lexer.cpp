#include "frontend/lexer.hpp"

#include <limits>
#include <string_view>

namespace minihls {
namespace {

bool isIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool isDigit(char c)      { return c >= '0' && c <= '9'; }
bool isIdentChar(char c)  { return isIdentStart(c) || isDigit(c); }

} // namespace

// driver

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> out;
  for (;;) {
    Token t = next();
    out.push_back(t);
    if (t.kind == Tok::Eof) return out;
  }
}

Token Lexer::next() {
  skipTrivia();
  if (pos_ >= src_.text().size())
    return Token{Tok::Eof, {{pos_}, {pos_}}, {}, 0, 0, false};

  char c = src_.text()[pos_];
  if (isIdentStart(c)) return lexWord();
  if (isDigit(c))      return lexNumber();
  if (c == '#')        return lexPragma();
  return lexOperator();
}

// whitespace, // comments, and /* */ comments
// Loop until the next real token. Block comments do NOT nest. On an
// unterminated one, report at the location of the opening `/*`, not at EOF —
// that's the location the user can act on.
void Lexer::skipTrivia() {
    while (pos_ < src_.text().size()) {
        char c = src_.text()[pos_];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++pos_;
            continue;
        }
        if (c == '/') {
            if (pos_ + 1 < src_.text().size()) {
                char next = src_.text()[pos_ + 1];
                if (next == '/') {
                    // single-line comment
                    pos_ += 2;
                    while (pos_ < src_.text().size() && src_.text()[pos_] != '\n') {
                        ++pos_;
                    }
                    continue;
                } else if (next == '*') {
                    // multi-line comment
                    uint32_t startPos = pos_;
                    pos_ += 2;
                    bool closed = false;
                    while (pos_ + 1 < src_.text().size()) {
                        if (src_.text()[pos_] == '*' && src_.text()[pos_ + 1] == '/') {
                            pos_ += 2;
                            closed = true;
                            break;
                        }
                        ++pos_;
                    }
                    if (!closed) {
                        diags_.error({{startPos}, {startPos + 2}}, "unterminated block comment");
                    }
                    continue;
                }
            }
        }
        break; // not trivia
    }
}

// identifier / keyword / type
// "Identifiers": consume the MAXIMAL run of identifier chars
// first, then classify the complete lexeme. Never split one.
//   - matches [iu][1-9][0-9]*  -> Tok::Type, fill width and isSigned.
//     A width outside 1..64 is still a Type token; report "width out of
//     range" rather than silently making it an identifier.
//   - matches a keyword        -> that keyword's Tok
//   - otherwise                -> Tok::Identifier
// So `i16x`, `u08` and `iota` are identifiers; `u8` is a type.
Token Lexer::lexWord() {
    const uint32_t start = pos_;
    while (pos_ < src_.text().size() && isIdentChar(src_.text()[pos_])) ++pos_;

    std::string_view text(src_.text().data() + start, pos_ - start);
    Token token{Tok::Identifier, {{start}, {pos_}}, text, 0, 0, false};

    static constexpr struct Keyword { std::string_view text; Tok kind; } keywords[] = {
        {"const", Tok::KwConst}, {"else", Tok::KwElse}, {"for", Tok::KwFor},
        {"if", Tok::KwIf}, {"in", Tok::KwIn}, {"out", Tok::KwOut},
        {"read", Tok::KwRead}, {"return", Tok::KwReturn},
        {"stream", Tok::KwStream}, {"write", Tok::KwWrite},
    };
    for (const auto& keyword : keywords) {
        if (text == keyword.text) {
            token.kind = keyword.kind;
            return token;
        }
    }

    bool isType = text.size() >= 2 && (text[0] == 'i' || text[0] == 'u') &&
                                text[1] >= '1' && text[1] <= '9';
    for (size_t i = 2; isType && i < text.size(); ++i)
        isType = isDigit(text[i]);
    if (isType) {
        unsigned width = 0;
        bool overflow = false;
        for (size_t i = 1; i < text.size(); ++i) {
            unsigned digit = static_cast<unsigned>(text[i] - '0');
            if (width > (std::numeric_limits<unsigned>::max() - digit) / 10)
                overflow = true;
            else if (!overflow)
                width = width * 10 + digit;
        }
        token.kind = Tok::Type;
        token.width = overflow ? std::numeric_limits<unsigned>::max() : width;
        token.isSigned = text[0] == 'i';
        if (overflow || token.width > 64)
            diags_.error(token.range, "type width out of range");
    }
    return token;
}

// integer literals
// Three bases: decimal, 0x/0X hex, 0b/0B binary. `_` is allowed BETWEEN
// digits only — never leading, never trailing, and `0x_1` is an error.
// A leading 0 is NOT octal: 0755 is 755.
// Accumulate into u128; if it overflows, report "integer literal too large".
Token Lexer::lexNumber() {
    const uint32_t start = pos_;
    unsigned base = 10;
    if (src_.text()[pos_] == '0' && pos_ + 1 < src_.text().size() &&
            (src_.text()[pos_ + 1] == 'x' || src_.text()[pos_ + 1] == 'X')) {
        base = 16;
        pos_ += 2;
    } else if (src_.text()[pos_] == '0' && pos_ + 1 < src_.text().size() &&
                         (src_.text()[pos_ + 1] == 'b' || src_.text()[pos_ + 1] == 'B')) {
        base = 2;
        pos_ += 2;
    }

    auto digitValue = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };

    u128 value = 0;
    bool hasDigit = false;
    bool error = false;
    while (pos_ < src_.text().size()) {
        char c = src_.text()[pos_];
        if (c == '_') {
            bool nextIsDigit = pos_ + 1 < src_.text().size() &&
                                                 digitValue(src_.text()[pos_ + 1]) >= 0 &&
                                                 digitValue(src_.text()[pos_ + 1]) < static_cast<int>(base);
            if (!hasDigit || !nextIsDigit) error = true;
            ++pos_;
            continue;
        }
        int digit = digitValue(c);
        if (digit < 0 || digit >= static_cast<int>(base)) break;
        hasDigit = true;
        if (value > (~static_cast<u128>(0) - static_cast<unsigned>(digit)) / base)
            error = true;
        else
            value = value * base + static_cast<unsigned>(digit);
        ++pos_;
    }

    if (!hasDigit || error)
        diags_.error({{start}, {pos_}}, hasDigit ? "invalid integer literal" :
                                                                                         "integer literal requires digits");
    return Token{Tok::IntLiteral, {{start}, {pos_}},
                             std::string_view(src_.text().data() + start, pos_ - start),
                             value, 0, false};
}

// #pragma
// The only newline-sensitive construct. Simplest: require the text `#pragma`,
// then take the rest of the line as the token's text and let the parser
// handle the body. Anything else starting with `#` is an error.
Token Lexer::lexPragma() {
    const uint32_t start = pos_;
    while (pos_ < src_.text().size() && src_.text()[pos_] != '\n') ++pos_;
    std::string_view text(src_.text().data() + start, pos_ - start);
    bool valid = text.size() >= 7 && text.substr(0, 7) == "#pragma" &&
                             (text.size() == 7 || text[7] == ' ' || text[7] == '\t' || text[7] == '\r');
    if (!valid) diags_.error({{start}, {pos_}}, "expected #pragma");
    return Token{Tok::Pragma, {{start}, {pos_}}, text, 0, 0, false};
}

// operators and punctuation
// Longest match matters here too: `<<` before `<`, `<=` before `<`, `&&`
// before `&`, `==` before `=`, `!=` before `!`. Get the order wrong and
// `a << 2` lexes as two `<`.
// An unrecognised character is an error; skip it and keep going so one bad
// byte doesn't suppress every later diagnostic.
Token Lexer::lexOperator() {
    const uint32_t start = pos_;
    const std::string& source = src_.text();
    auto make = [&](Tok kind, unsigned length) {
        pos_ += length;
        return Token{kind, {{start}, {pos_}},
                                 std::string_view(source.data() + start, length), 0, 0, false};
    };

    if (pos_ + 1 < source.size()) {
        std::string_view two(source.data() + pos_, 2);
        if (two == "<<") return make(Tok::Shl, 2);
        if (two == ">>") return make(Tok::Shr, 2);
        if (two == "<=") return make(Tok::Le, 2);
        if (two == ">=") return make(Tok::Ge, 2);
        if (two == "==") return make(Tok::EqEq, 2);
        if (two == "!=") return make(Tok::BangEq, 2);
        if (two == "&&") return make(Tok::AmpAmp, 2);
        if (two == "||") return make(Tok::PipePipe, 2);
    }

    switch (source[pos_]) {
        case '+': return make(Tok::Plus, 1); case '-': return make(Tok::Minus, 1);
        case '*': return make(Tok::Star, 1); case '/': return make(Tok::Slash, 1);
        case '%': return make(Tok::Percent, 1); case '~': return make(Tok::Tilde, 1);
        case '&': return make(Tok::Amp, 1); case '|': return make(Tok::Pipe, 1);
        case '^': return make(Tok::Caret, 1); case '!': return make(Tok::Bang, 1);
        case '<': return make(Tok::Lt, 1); case '>': return make(Tok::Gt, 1);
        case '=': return make(Tok::Assign, 1); case '?': return make(Tok::Question, 1);
        case ':': return make(Tok::Colon, 1); case '(': return make(Tok::LParen, 1);
        case ')': return make(Tok::RParen, 1); case '{': return make(Tok::LBrace, 1);
        case '}': return make(Tok::RBrace, 1); case '[': return make(Tok::LBracket, 1);
        case ']': return make(Tok::RBracket, 1); case ',': return make(Tok::Comma, 1);
        case ';': return make(Tok::Semi, 1);
        default:
            diags_.error({{start}, {start + 1}}, "unrecognised character");
            ++pos_;
            return next();
    }
}

} // namespace minihls