#include "frontend/lexer.hpp"

#include "frontend/diagnostics.hpp"
#include "frontend/source.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using minihls::DiagnosticEngine;
using minihls::Lexer;
using minihls::SourceFile;
using minihls::Token;
using minihls::TokenKind;

// Owns the file and diagnostics for the lifetime of a test, since
// DiagnosticEngine and the token text views both borrow from the file.
class Lexed {
 public:
  explicit Lexed(std::string text)
      : file_("test.hc", std::move(text)), diagnostics_(file_) {
    tokens_ = Lexer(file_, diagnostics_).tokenize();
  }

  const std::vector<Token>& tokens() const { return tokens_; }
  const DiagnosticEngine& diagnostics() const { return diagnostics_; }

  // Token kinds excluding the trailing EndOfFile, for compact comparisons.
  std::vector<TokenKind> kinds() const {
    std::vector<TokenKind> result;
    for (const Token& token : tokens_) {
      if (token.kind == TokenKind::EndOfFile) break;
      result.push_back(token.kind);
    }
    return result;
  }

 private:
  SourceFile file_;
  DiagnosticEngine diagnostics_;
  std::vector<Token> tokens_;
};

TEST(Lexer, AlwaysTerminatesWithExactlyOneEndOfFile) {
  const Lexed empty("");
  ASSERT_EQ(empty.tokens().size(), 1u);
  EXPECT_EQ(empty.tokens().front().kind, TokenKind::EndOfFile);

  const Lexed some("x");
  ASSERT_EQ(some.tokens().size(), 2u);
  EXPECT_EQ(some.tokens().back().kind, TokenKind::EndOfFile);
}

TEST(Lexer, RecognizesTypeTokensAndCarriesWidthAndSignedness) {
  const Lexed lexed("i8 u7 i32 i1 u64");
  const std::vector<Token>& tokens = lexed.tokens();
  ASSERT_GE(tokens.size(), 5u);
  EXPECT_FALSE(lexed.diagnostics().has_errors());

  EXPECT_EQ(tokens[0].kind, TokenKind::IntType);
  EXPECT_EQ(tokens[0].width, 8u);
  EXPECT_TRUE(tokens[0].is_signed);

  EXPECT_EQ(tokens[1].width, 7u);
  EXPECT_FALSE(tokens[1].is_signed);

  EXPECT_EQ(tokens[2].width, 32u);
  EXPECT_EQ(tokens[3].width, 1u);
  EXPECT_EQ(tokens[4].width, 64u);
  EXPECT_FALSE(tokens[4].is_signed);
}

// The reserved type spelling is exactly `[iu][0-9]+`. Anything else that merely
// starts with those characters stays an ordinary identifier -- which matters
// because `i` is the conventional loop counter.
TEST(Lexer, DoesNotMistakeIdentifiersForTypes) {
  const Lexed lexed("i u index i32_total u7x iii");
  EXPECT_FALSE(lexed.diagnostics().has_errors());
  for (const TokenKind kind : lexed.kinds()) {
    EXPECT_EQ(kind, TokenKind::Identifier);
  }
}

TEST(Lexer, RejectsWidthsOutsideTheSupportedRange) {
  const Lexed zero("i0");
  EXPECT_TRUE(zero.diagnostics().has_errors());
  EXPECT_TRUE(zero.diagnostics().contains("bit width must be between 1 and 64"));

  const Lexed too_wide("u65");
  EXPECT_TRUE(too_wide.diagnostics().has_errors());

  const Lexed absurd("i999999999999999999999");
  EXPECT_TRUE(absurd.diagnostics().has_errors());
}

TEST(Lexer, RecognizesKeywords) {
  const Lexed lexed("if else for return const stream pragma void");
  EXPECT_FALSE(lexed.diagnostics().has_errors());
  const std::vector<TokenKind> expected = {
      TokenKind::KwIf,     TokenKind::KwElse,   TokenKind::KwFor,
      TokenKind::KwReturn, TokenKind::KwConst,  TokenKind::KwStream,
      TokenKind::KwPragma, TokenKind::KwVoid,
  };
  EXPECT_EQ(lexed.kinds(), expected);
}

TEST(Lexer, ParsesIntegerLiteralsInEveryBase) {
  const Lexed lexed("0 64 255 0xFF 0xff 0b1011 0b1010_1010 1_000");
  EXPECT_FALSE(lexed.diagnostics().has_errors());
  const std::vector<Token>& tokens = lexed.tokens();
  ASSERT_GE(tokens.size(), 8u);
  EXPECT_EQ(tokens[0].value, 0u);
  EXPECT_EQ(tokens[1].value, 64u);
  EXPECT_EQ(tokens[2].value, 255u);
  EXPECT_EQ(tokens[3].value, 255u);
  EXPECT_EQ(tokens[4].value, 255u);
  EXPECT_EQ(tokens[5].value, 11u);
  EXPECT_EQ(tokens[6].value, 170u);
  EXPECT_EQ(tokens[7].value, 1000u);
}

TEST(Lexer, RejectsMalformedIntegerLiterals) {
  // Splitting this into `123` and `abc` would be worse than rejecting it.
  const Lexed trailing("123abc");
  EXPECT_TRUE(trailing.diagnostics().contains("invalid digit"));

  // `2` is not a binary digit.
  const Lexed wrong_base("0b12");
  EXPECT_TRUE(wrong_base.diagnostics().contains("invalid digit"));

  const Lexed no_digits("0x");
  EXPECT_TRUE(no_digits.diagnostics().contains("no digits"));

  const Lexed overflow("99999999999999999999999999");
  EXPECT_TRUE(overflow.diagnostics().contains("does not fit in 64 bits"));
}

TEST(Lexer, HandlesTheLargestRepresentableLiteral) {
  const Lexed lexed("18446744073709551615");
  EXPECT_FALSE(lexed.diagnostics().has_errors());
  EXPECT_EQ(lexed.tokens().front().value, 18446744073709551615ull);
}

TEST(Lexer, SkipsComments) {
  const Lexed lexed(R"(
    // a line comment
    x /* an inline comment */ y
    /* multi
       line */ z
  )");
  EXPECT_FALSE(lexed.diagnostics().has_errors());
  const std::vector<TokenKind> expected = {
      TokenKind::Identifier, TokenKind::Identifier, TokenKind::Identifier};
  EXPECT_EQ(lexed.kinds(), expected);
}

TEST(Lexer, ReportsUnterminatedBlockComment) {
  const Lexed lexed("x /* never closed");
  EXPECT_TRUE(lexed.diagnostics().contains("unterminated block comment"));
}

// Two-character operators must win over their one-character prefixes, or `<=`
// would silently become `<` followed by `=`.
TEST(Lexer, PrefersLongerOperators) {
  const Lexed lexed("== != <= >= << >> && || = < > & | !");
  EXPECT_FALSE(lexed.diagnostics().has_errors());
  const std::vector<TokenKind> expected = {
      TokenKind::EqualEqual, TokenKind::BangEqual,     TokenKind::LessEqual,
      TokenKind::GreaterEqual, TokenKind::LessLess,    TokenKind::GreaterGreater,
      TokenKind::AmpAmp,     TokenKind::PipePipe,      TokenKind::Assign,
      TokenKind::Less,       TokenKind::Greater,       TokenKind::Amp,
      TokenKind::Pipe,       TokenKind::Bang,
  };
  EXPECT_EQ(lexed.kinds(), expected);
}

TEST(Lexer, ReportsUnexpectedCharacters) {
  const Lexed lexed("x $ y");
  EXPECT_TRUE(lexed.diagnostics().contains("unexpected character '$'"));
  // Lexing continues, so the tokens after the bad character are still produced.
  EXPECT_EQ(lexed.kinds().size(), 3u);
}

TEST(Lexer, TracksSourceLocations) {
  const std::string text = "i32 x\n  = 5;\n";
  SourceFile file("test.hc", text);
  DiagnosticEngine diagnostics(file);
  const std::vector<Token> tokens = Lexer(file, diagnostics).tokenize();
  ASSERT_GE(tokens.size(), 4u);

  EXPECT_EQ(file.resolve(tokens[0].range.begin).line, 1u);
  EXPECT_EQ(file.resolve(tokens[0].range.begin).column, 1u);
  // `x` is the fifth character of line 1.
  EXPECT_EQ(file.resolve(tokens[1].range.begin).column, 5u);
  // `=` begins line 2 at column 3.
  EXPECT_EQ(file.resolve(tokens[2].range.begin).line, 2u);
  EXPECT_EQ(file.resolve(tokens[2].range.begin).column, 3u);
}

}  // namespace
