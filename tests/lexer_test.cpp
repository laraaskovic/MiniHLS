#include <gtest/gtest.h>
#include <iostream>
#include "frontend/lexer.hpp"
using namespace minihls;

static std::vector<Token> lex(const std::string& text, bool* hadError = nullptr) {
  static std::vector<std::unique_ptr<SourceFile>> keep;   // tokens hold views
  keep.push_back(std::make_unique<SourceFile>("<test>", text));
  static std::vector<std::unique_ptr<Diagnostics>> dkeep;
  dkeep.push_back(std::make_unique<Diagnostics>(*keep.back()));
  auto toks = Lexer(*keep.back(), *dkeep.back()).tokenize();
  if (hadError) *hadError = dkeep.back()->hasErrors();
  return toks;
}

static std::vector<Tok> kinds(const std::string& text) {
  std::vector<Tok> k;
  for (auto& t : lex(text)) k.push_back(t.kind);
  return k;
}

TEST(Lexer, EmptyInputIsJustEof) {
  EXPECT_EQ(kinds(""), (std::vector<Tok>{Tok::Eof}));
}

// the whole-lexeme classification rule

TEST(Lexer, TypesAreOneToken) {
  auto t = lex("i16 u1 i64");
  ASSERT_EQ(t.size(), 4u);
  EXPECT_EQ(t[0].kind, Tok::Type);  EXPECT_EQ(t[0].width, 16u); EXPECT_TRUE(t[0].isSigned);
  EXPECT_EQ(t[1].kind, Tok::Type);  EXPECT_EQ(t[1].width, 1u);  EXPECT_FALSE(t[1].isSigned);
  EXPECT_EQ(t[2].kind, Tok::Type);  EXPECT_EQ(t[2].width, 64u);
}

TEST(Lexer, TypePrefixDoesNotSplitAnIdentifier) {
  // i16x is ONE identifier, not Type(i16) + Identifier(x)
  EXPECT_EQ(kinds("i16x"), (std::vector<Tok>{Tok::Identifier, Tok::Eof}));
  EXPECT_EQ(kinds("u8_mask"), (std::vector<Tok>{Tok::Identifier, Tok::Eof}));
  EXPECT_EQ(kinds("iota"), (std::vector<Tok>{Tok::Identifier, Tok::Eof}));
}

TEST(Lexer, LeadingZeroWidthIsAnIdentifier) {
  EXPECT_EQ(kinds("u08"), (std::vector<Tok>{Tok::Identifier, Tok::Eof}));
}

TEST(Lexer, KeywordsAreNotIdentifiers) {
  EXPECT_EQ(kinds("for if else return const stream in out read write"),
            (std::vector<Tok>{Tok::KwFor, Tok::KwIf, Tok::KwElse, Tok::KwReturn,
                              Tok::KwConst, Tok::KwStream, Tok::KwIn, Tok::KwOut,
                              Tok::KwRead, Tok::KwWrite, Tok::Eof}));
}

// literals

TEST(Lexer, LiteralBases) {
  auto t = lex("42 0xFF 0b1010 0755");
  EXPECT_EQ(t[0].intValue, static_cast<u128>(42));
  EXPECT_EQ(t[1].intValue, static_cast<u128>(255));
  EXPECT_EQ(t[2].intValue, static_cast<u128>(10));
  EXPECT_EQ(t[3].intValue, static_cast<u128>(755));   // NOT octal
}

TEST(Lexer, UnderscoreSeparatorsAreIgnored) {
  auto t = lex("1_000_000 0xDEAD_BEEF 0b1111_0000");
  EXPECT_EQ(t[0].intValue, static_cast<u128>(1000000));
  EXPECT_EQ(t[1].intValue, static_cast<u128>(0xDEADBEEF));
  EXPECT_EQ(t[2].intValue, static_cast<u128>(0xF0));
}

TEST(Lexer, TrailingUnderscoreIsAnError) {
  bool err = false; lex("100_", &err); EXPECT_TRUE(err);
}

TEST(Lexer, InvalidDigitsStayInOneIntegerToken) {
  bool err = false;
  auto tokens = lex("0b12 123abc", &err);
  EXPECT_TRUE(err);
  ASSERT_EQ(tokens.size(), 3u);
  EXPECT_EQ(tokens[0].kind, Tok::IntLiteral);
  EXPECT_EQ(tokens[0].text, "0b12");
  EXPECT_EQ(tokens[1].kind, Tok::IntLiteral);
  EXPECT_EQ(tokens[1].text, "123abc");
  EXPECT_EQ(tokens[2].kind, Tok::Eof);
}

TEST(Lexer, OverflowHasItsOwnDiagnostic) {
  SourceFile source("<test>", "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  Diagnostics diagnostics(source);
  auto tokens = Lexer(source, diagnostics).tokenize();
  ASSERT_EQ(tokens.size(), 2u);
  ASSERT_EQ(diagnostics.all().size(), 1u);
  EXPECT_EQ(diagnostics.all()[0].message, "integer literal too large");
}

// longest-match operators

TEST(Lexer, MultiCharOperatorsWinOverSingle) {
  EXPECT_EQ(kinds("<< >> <= >= == != && ||"),
            (std::vector<Tok>{Tok::Shl, Tok::Shr, Tok::Le, Tok::Ge,
                              Tok::EqEq, Tok::BangEq, Tok::AmpAmp, Tok::PipePipe,
                              Tok::Eof}));
  EXPECT_EQ(kinds("< > = & | !"),
            (std::vector<Tok>{Tok::Lt, Tok::Gt, Tok::Assign, Tok::Amp,
                              Tok::Pipe, Tok::Bang, Tok::Eof}));
}

// comments and trivia

TEST(Lexer, CommentsProduceNoTokens) {
  EXPECT_EQ(kinds("// all of this\n/* and this */ 1"),
            (std::vector<Tok>{Tok::IntLiteral, Tok::Eof}));
}

TEST(Lexer, BlockCommentsDoNotNest) {
  // The first */ closes it, so `*/` at the end is two operators.
  auto k = kinds("/* /* */ 1");
  EXPECT_EQ(k[0], Tok::IntLiteral);
}

TEST(Lexer, UnterminatedBlockCommentProducesNoTokens) {
  bool err = false;
  auto tokens = lex("/* forever", &err);
  EXPECT_TRUE(err);
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, Tok::Eof);
}

TEST(Lexer, ManyInvalidCharactersDoNotRecurse) {
  bool err = false;
  std::string input(100000, '@');
  auto tokens = lex(input, &err);
  EXPECT_TRUE(err);
  ASSERT_EQ(tokens.size(), 1u);
  EXPECT_EQ(tokens[0].kind, Tok::Eof);
}

// locations

TEST(Lexer, LineAndColumnAreOneBased) {
  SourceFile src("<test>", "i16 a;\n  u8 b;\n");
  Diagnostics d(src);
  auto t = Lexer(src, d).tokenize();
  EXPECT_EQ(src.line(t[0].range.begin), 1u);
  EXPECT_EQ(src.column(t[0].range.begin), 1u);
  // the `u8` on line 2, indented two spaces
  EXPECT_EQ(src.line(t[3].range.begin), 2u);
  EXPECT_EQ(src.column(t[3].range.begin), 3u);
}

// pragmas

TEST(Lexer, PragmaIsOneTokenToEndOfLine) {
  EXPECT_EQ(kinds("#pragma pipeline II=1\nfor"),
            (std::vector<Tok>{Tok::Pragma, Tok::KwFor, Tok::Eof}));
}

TEST(Lexer, PrintsAbsDiffExampleTokens) {
  auto source = SourceFile::load(std::string(MINIHLS_SOURCE_DIR) +
                                 "/examples/abs_diff.hc");
  ASSERT_TRUE(source.has_value());

  Diagnostics diagnostics(*source);
  auto tokens = Lexer(*source, diagnostics).tokenize();
  for (const Token& token : tokens)
    std::cout << tokName(token.kind) << " [" << token.text << "]\n";

  EXPECT_FALSE(diagnostics.hasErrors());
  ASSERT_FALSE(tokens.empty());
  EXPECT_EQ(tokens.back().kind, Tok::Eof);
}