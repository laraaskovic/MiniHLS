#include "frontend/parser.hpp"

#include <cctype>
#include <limits>
#include <string_view>

namespace minihls {

// token helpers

const Token& Parser::peek(size_t ahead) const {
  size_t i = pos_ + ahead;
  return i < toks_.size() ? toks_[i] : toks_.back();   // back() is always Eof
}
bool Parser::check(Tok k, size_t ahead) const { return peek(ahead).kind == k; }
bool Parser::accept(Tok k) { if (check(k)) { ++pos_; return true; } return false; }
Token Parser::advance() { Token t = peek(); if (pos_ < toks_.size() - 1) ++pos_; return t; }

Token Parser::expect(Tok k, const char* what) {
  if (check(k)) return advance();
  diags_.error(peek().range, std::string("expected ") + what);
  return peek();                            // don't consume; let recovery handle it
}

// expressions

// Higher binds tighter
static int bindingPower(Tok op) {
  switch (op) {
    case Tok::PipePipe: return 1;
    case Tok::AmpAmp:   return 2;
    case Tok::Lt: case Tok::Le: case Tok::Gt:
    case Tok::Ge: case Tok::EqEq: case Tok::BangEq: return 3;
    case Tok::Plus: case Tok::Minus: case Tok::Pipe: case Tok::Caret: return 4;
    case Tok::Star: case Tok::Slash: case Tok::Percent:
    case Tok::Shl:  case Tok::Shr:   case Tok::Amp: return 5;
    default: return 0;
  }
}

static Range span(const Expr& a, const Expr& b) { return {a.range.begin, b.range.end}; }

ExprPtr Parser::parseConditional() {
  ExprPtr cond = parseExpr(0);
  if (!accept(Tok::Question)) return cond;
  ExprPtr t = parseExpr(0);                       // unconstrained between ? and :
  expect(Tok::Colon, "':' in a conditional expression");
  ExprPtr f = parseConditional();                 // recurse -> right-associative
  Range r = span(*cond, *f);
  return std::make_unique<Ternary>(r, std::move(cond), std::move(t), std::move(f));
}

ExprPtr Parser::parseExpr(int minBP) {
  ExprPtr lhs = parseUnary();
  for (;;) {
    int bp = bindingPower(peek().kind);
    if (bp == 0 || bp < minBP) break;
    Token op = advance();
    ExprPtr rhs = parseExpr(bp + 1);              // +1 => left-associative
    Range r = span(*lhs, *rhs);
    lhs = std::make_unique<Binary>(r, op.kind, std::move(lhs), std::move(rhs));
  }
  return lhs;
}

ExprPtr Parser::parseExpressionOnly() { return parseConditional(); }

// unary operators
// LANGUAGE.md level 2: - + ~ ! , RIGHT-associative, so recurse into
// parseUnary() (not parsePostfix) for the operand: `--x` and `!!b` must work.
// Otherwise fall through to parsePostfix().
ExprPtr Parser::parseUnary() { 
  if (check(Tok::Minus) || check(Tok::Plus) || check(Tok::Tilde) || check(Tok::Bang)) {
    Token op = advance();
    ExprPtr operand = parseUnary();
    if (!operand) return nullptr;
    Range r{op.range.begin, operand->range.end};
    return std::make_unique<Unary>(r, op.kind, std::move(operand));
  }
  return parsePostfix();
}

// postfix
// Your grammar: `identifier "[" expression "]"` and NOTHING else — no
// a[i][j], no u8(x)[0]. So: if the next two tokens are Identifier then
// LBracket, build an Index; otherwise defer to parsePrimary().
ExprPtr Parser::parsePostfix() {
  if (check(Tok::Identifier) && check(Tok::LBracket, 1)) {
    Token name = advance();
    advance();
    ExprPtr index = parseConditional();
    Token close = expect(Tok::RBracket, "']' in an array index");
    if (!index) return nullptr;
    return std::make_unique<Index>(Range{name.range.begin, close.range.end},
                                   std::string(name.text), std::move(index));
  }
  return parsePrimary();
}

// primary
//   IntLiteral         -> IntLit (the lexer already decoded the value)
//   Type               -> a CAST: expect '(' expr ')'. A Type token can never
//                         start an identifier, so one token settles cast-vs-name.
//   Identifier         -> NameRef
//   '('                -> parse an expression, expect ')'
//   anything else      -> error "expected an expression"
ExprPtr Parser::parsePrimary() {
  Token t = peek();
  switch (t.kind) {
    case Tok::IntLiteral:
      advance(); return std::make_unique<IntLit>(t.range, t.intValue);
    case Tok::Type: {
      advance();
      expect(Tok::LParen, "'(' after a type for a cast");
      ExprPtr operand = parseConditional();
      Token close = expect(Tok::RParen, "')' after a cast expression");
      if (!operand) return nullptr;
      return std::make_unique<Cast>(Range{t.range.begin, close.range.end},
                                    Type{t.width, t.isSigned}, std::move(operand));
    }
    case Tok::Identifier:
      advance(); return std::make_unique<NameRef>(t.range, std::string(t.text));
    case Tok::LParen: {
      advance(); ExprPtr e = parseConditional();
      expect(Tok::RParen, "')' after an expression"); return e;
    }
    default:
      diags_.error(t.range, "expected an expression");
      if (!check(Tok::Eof)) advance();
      return nullptr;
  }
}

// statements

// expect '{', loop parseStatement() until '}' or Eof, expect '}'.
BlockPtr Parser::parseBlock() {
  Token open = expect(Tok::LBrace, "'{' to start a block");
  std::vector<StmtPtr> statements;

  // Each nested parseBlock consumes its own closing brace, so this loop
  // stops only on the brace belonging to the current block.
  while (!check(Tok::RBrace) && !check(Tok::Eof)) {
    size_t before = pos_;
    StmtPtr statement = parseStatement();
    if (statement) statements.push_back(std::move(statement));

    // Ensure malformed input cannot leave the parser stuck.
    if (pos_ == before) advance();
  }

  Token close = expect(Tok::RBrace, "'}' to end a block");
  auto block = std::make_unique<Block>(Range{open.range.begin, close.range.end});
  block->stmts = std::move(statements);
  return block;
}

// dispatch on the first token
//   Tok::Type       -> parseDeclaration()   (see its note about lookahead)
//   Tok::KwIf       -> parseIf()
//   Tok::Pragma or Tok::KwFor -> parseFor()
//   Tok::KwReturn   -> parseReturn()
//   Tok::KwWrite    -> parseWrite()
//   Tok::LBrace     -> parseBlock()
//   Tok::Identifier -> parseAssign()
//   otherwise       -> error, then SKIP TOKENS UNTIL ';' or '}' so one bad
//                      statement doesn't cascade into a hundred errors.
StmtPtr Parser::parseStatement() {
  switch (peek().kind) {
    case Tok::Type: return parseDeclaration();
    case Tok::KwIf: return parseIf();
    case Tok::KwFor: case Tok::Pragma: return parseFor();
    case Tok::KwReturn: return parseReturn();
    case Tok::KwWrite: return parseWrite();
    case Tok::LBrace: return parseBlock();
    case Tok::Identifier: return parseAssign();
    default:
      diags_.error(peek().range, "expected a statement");
      while (!check(Tok::Semi) && !check(Tok::RBrace) && !check(Tok::Eof)) advance();
      accept(Tok::Semi);
      return nullptr;
  }
}

// var_decl vs array_decl
// Both start Type Identifier. Look TWO ahead: check(Tok::LBracket, 2) means
// an array. This is the only place you need 2-token lookahead.
//   scalar: Type Ident '=' (expression | read(s)) ';'
//           an initialiser is REQUIRED — LANGUAGE.md has no uninitialised scalars
//   array:  Type Ident '[' const_expr ']' [ '=' '{' expr {',' expr} [','] '}' ] ';'
StmtPtr Parser::parseDeclaration() {
  Token typeToken = expect(Tok::Type, "a type");
  Token name = expect(Tok::Identifier, "an identifier");
  Type type{typeToken.width, typeToken.isSigned};
  if (accept(Tok::LBracket)) {
    auto node = std::make_unique<ArrayDecl>(Range{typeToken.range.begin, name.range.end});
    node->elem = type; node->name = std::string(name.text);
    node->size = parseConditional();
    expect(Tok::RBracket, "']' after an array size");
    if (accept(Tok::Assign)) {
      expect(Tok::LBrace, "'{' before an array initializer");
      if (!check(Tok::RBrace)) {
        do {
          ExprPtr value = parseConditional();
          if (value) node->init.push_back(std::move(value));
        } while (accept(Tok::Comma) && !check(Tok::RBrace));
      }
      expect(Tok::RBrace, "'}' after an array initializer");
    }
    Token semi = expect(Tok::Semi, "';' after an array declaration");
    node->range.end = semi.range.end;
    return node;
  }
  auto node = std::make_unique<VarDecl>(Range{typeToken.range.begin, name.range.end});
  node->type = type; node->name = std::string(name.text);
  expect(Tok::Assign, "'=' in a declaration");
  if (accept(Tok::KwRead)) {
    node->initIsRead = true;
    expect(Tok::LParen, "'(' after read");
    Token stream = expect(Tok::Identifier, "a stream identifier");
    node->readStream = std::string(stream.text);
    expect(Tok::RParen, "')' after read");
  } else node->init = parseConditional();
  Token semi = expect(Tok::Semi, "';' after a declaration");
  node->range.end = semi.range.end;
  return node;
}

// // Identifier [ '[' expression ']' ] '=' ( expression | read(s) ) ';'
// `read(s)` is legal ONLY as the whole right-hand side — never nested inside a
// larger expression. That restriction is what keeps expressions pure, so check
// for Tok::KwRead here rather than in parsePrimary().
StmtPtr Parser::parseAssign() {
  Token name = expect(Tok::Identifier, "an assignment target");
  auto node = std::make_unique<Assign>(name.range);
  node->name = std::string(name.text);
  if (accept(Tok::LBracket)) {
    node->index = parseConditional();
    expect(Tok::RBracket, "']' after an array index");
  }
  expect(Tok::Assign, "'=' in an assignment");
  if (accept(Tok::KwRead)) {
    node->valueIsRead = true;
    expect(Tok::LParen, "'(' after read");
    Token stream = expect(Tok::Identifier, "a stream identifier");
    node->readStream = std::string(stream.text);
    expect(Tok::RParen, "')' after read");
  } else node->value = parseConditional();
  Token semi = expect(Tok::Semi, "';' after an assignment");
  node->range.end = semi.range.end;
  return node;
}

//  write '(' identifier ',' expression ')' ';'
StmtPtr Parser::parseWrite() {
  Token start = expect(Tok::KwWrite, "write");
  auto node = std::make_unique<Write>(start.range);
  expect(Tok::LParen, "'(' after write");
  Token stream = expect(Tok::Identifier, "a stream identifier");
  node->stream = std::string(stream.text);
  expect(Tok::Comma, "',' after the stream");
  node->value = parseConditional();
  expect(Tok::RParen, "')' after write");
  Token semi = expect(Tok::Semi, "';' after write");
  node->range.end = semi.range.end;
  return node;
}

// // 'if' '(' expression ')' block [ 'else' ( block | if_stmt ) ]
// Braces are mandatory on both arms, so `else` is followed by either '{' or
// another 'if' — recurse for `else if` chains. No dangling-else case exists.
StmtPtr Parser::parseIf() {
  Token start = expect(Tok::KwIf, "if");
  auto node = std::make_unique<If>(start.range);
  expect(Tok::LParen, "'(' after if"); node->cond = parseConditional();
  expect(Tok::RParen, "')' after the if condition");
  node->thenB = parseBlock();
  if (accept(Tok::KwElse)) node->elseS = check(Tok::KwIf) ? parseIf() : parseBlock();
  node->range.end = node->elseS ? node->elseS->range.end : node->thenB->range.end;
  return node;
}

// the rigid header
// [pragma] 'for' '(' for_init ';' for_cond ';' for_step ')' block
//   for_init: Type Identifier '=' const_expr
//   for_cond: Identifier relOp const_expr        (relOp: < <= > >=)
//   for_step: Identifier '=' Identifier ('+'|'-') const_expr
// Parse these STRUCTURALLY into the For fields — do not parse a general
// expression and inspect it afterwards. Reject anything else here with a
// message naming which of the three parts was wrong.
// Checking that the two identifiers match the induction variable is a
// SEMANTIC check (E3), not the parser's job — but record all three names so
// sema can compare them.
StmtPtr Parser::parseFor() {
  Pragma pragma;
  if (check(Tok::Pragma)) pragma = parsePragma(advance());
  Token start = expect(Tok::KwFor, "for");
  auto node = std::make_unique<For>(start.range); node->pragma = pragma;
  expect(Tok::LParen, "'(' after for");
  node->ivType = parseType("the loop variable type");
  Token iv = expect(Tok::Identifier, "the loop variable");
  node->iv = std::string(iv.text);
  expect(Tok::Assign, "'=' in the for initializer"); node->init = parseConditional();
  expect(Tok::Semi, "';' after the for initializer");
  Token condName = expect(Tok::Identifier, "the loop condition variable");
  if (condName.text != node->iv) {
    diags_.error(condName.range, "condition must test the loop variable '" + node->iv + "'");
  }
  if (check(Tok::Lt) || check(Tok::Le) || check(Tok::Gt) || check(Tok::Ge))
    node->relOp = advance().kind;
  else
    diags_.error(peek().range, "expected '<', '<=', '>' or '>=' in the for condition");
  node->limit = parseConditional();
  expect(Tok::Semi, "';' after the for condition");
  Token stepName = expect(Tok::Identifier, "the loop step variable");
  if (stepName.text != node->iv) {
    diags_.error(stepName.range, "step must update the loop variable '" + node->iv + "'");
  }
  expect(Tok::Assign, "'=' in the for step");
  Token stepBase = expect(Tok::Identifier, "the loop step operand");
  if (stepBase.text != node->iv) {
    diags_.error(stepBase.range, "step must update the loop variable '" + node->iv + "'");
  }
  if (!check(Tok::Plus) && !check(Tok::Minus))
    diags_.error(peek().range, "expected '+' or '-' in the for step");
  node->stepIsAdd = check(Tok::Plus);
  if (check(Tok::Plus) || check(Tok::Minus)) advance();
  node->step = parseConditional();
  expect(Tok::RParen, "')' after the for header");
  node->body = parseBlock(); node->range.end = node->body->range.end;
  return node;
}

//  'return' expression ';'
StmtPtr Parser::parseReturn() {
  Token start = expect(Tok::KwReturn, "return");
  auto node = std::make_unique<Return>(start.range);
  node->value = parseConditional();
  Token semi = expect(Tok::Semi, "';' after return"); node->range.end = semi.range.end;
  return node;
}

//==== program====

// // Split the pragma token's text: "#pragma unroll", "#pragma unroll factor=N",
// "#pragma pipeline II=N". An unrecognised pragma is an ERROR, not a warning.
Pragma Parser::parsePragma(Token t) {
  Pragma result;
  result.range = t.range;
  std::string text(t.text);
  auto starts = [&](std::string_view value) { return text.rfind(value, 0) == 0; };
  auto parseValue = [&](size_t at) -> unsigned {
    unsigned value = 0;
    bool any = false;
    for (; at < text.size(); ++at) {
      if (!std::isdigit(static_cast<unsigned char>(text[at]))) {
        diags_.error(t.range, "invalid pragma value");
        return 0;
      }
      any = true;
      unsigned digit = static_cast<unsigned>(text[at] - '0');
      if (value > (std::numeric_limits<unsigned>::max() - digit) / 10) {
        diags_.error(t.range, "pragma value too large");
        return 0;
      }
      value = value * 10 + digit;
    }
    if (!any) diags_.error(t.range, "pragma requires a value");
    return value;
  };
  if (text == "#pragma unroll") {
    result.kind = PragmaKind::Unroll;
    return result;
  }
  if (starts("#pragma unroll factor=")) {
    result.kind = PragmaKind::Unroll;
    result.hasValue = true;
    result.value = parseValue(22);
    return result;
  }
  if (starts("#pragma pipeline II=")) {
    result.kind = PragmaKind::Pipeline;
    result.hasValue = true;
    result.value = parseValue(20);
    return result;
  }
  diags_.error(t.range, "unrecognised pragma");
  return result;
}

//  the Type token already carries width and isSigned.
Type Parser::parseType(const char* what) {
  Token token = expect(Tok::Type, what);
  return Type{token.width, token.isSigned};
}

// //   Type Identifier                                  -> Scalar
//   Type Identifier '[' const_expr ']'               -> Array
//   ('in'|'out') 'stream' '<' Type '>' Identifier    -> StreamIn / StreamOut
Param Parser::parseParam() {
  Param param;
  Token start = peek();
  param.range = start.range;
  if (check(Tok::KwIn) || check(Tok::KwOut)) {
    Tok direction = advance().kind;
    param.kind = direction == Tok::KwIn ? ParamKind::StreamIn : ParamKind::StreamOut;
    expect(Tok::KwStream, "stream");
    expect(Tok::Lt, "'<' after stream");
    param.type = parseType("the stream element type");
    expect(Tok::Gt, "'>' after the stream type");
    Token name = expect(Tok::Identifier, "the stream name");
    param.name = std::string(name.text);
    param.range.end = name.range.end;
    return param;
  }
  param.type = parseType("parameter type");
  Token name = expect(Tok::Identifier, "parameter name");
  param.name = std::string(name.text);
  if (accept(Tok::LBracket)) {
    param.kind = ParamKind::Array;
    param.size = parseConditional();
    Token close = expect(Tok::RBracket, "']' after parameter size");
    param.range.end = close.range.end;
  } else {
    param.range.end = name.range.end;
  }
  return param;
}

// //   'const' Type Identifier '=' expression ';'
//   'const' Type Identifier '[' const_expr ']' '=' array_init ';'
ConstDecl Parser::parseConstDecl() {
  Token start = expect(Tok::KwConst, "const");
  ConstDecl result;
  result.range = start.range;
  result.type = parseType("constant type");
  Token name = expect(Tok::Identifier, "constant name");
  result.name = std::string(name.text);
  if (accept(Tok::LBracket)) {
    result.isArray = true;
    result.size = parseConditional();
    expect(Tok::RBracket, "']' after constant array size");
    expect(Tok::Assign, "'=' before constant array initializer");
    expect(Tok::LBrace, "'{' before constant array initializer");
    if (!check(Tok::RBrace)) {
      do {
        auto value = parseConditional();
        if (value) result.arrayInit.push_back(std::move(value));
      } while (accept(Tok::Comma) && !check(Tok::RBrace));
    }
    Token close = expect(Tok::RBrace, "'}' after constant array initializer");
    result.range.end = close.range.end;
  } else {
    expect(Tok::Assign, "'=' in a constant declaration");
    result.init = parseConditional();
  }
  Token semi = expect(Tok::Semi, "';' after a constant declaration");
  result.range.end = semi.range.end;
  return result;
}

//  Type Identifier '(' [param {',' param}] ')' block
Function Parser::parseFunction() {
  Function result;
  Token type = peek();
  result.range.begin = type.range.begin;
  result.returnType = parseType("return type");
  Token name = expect(Tok::Identifier, "function name");
  result.name = std::string(name.text);
  expect(Tok::LParen, "'(' after function name");
  if (!check(Tok::RParen)) {
    do {
      result.params.push_back(parseParam());
    } while (accept(Tok::Comma));
  }
  expect(Tok::RParen, "')' after function parameters");
  result.body = parseBlock();
  result.range.end = result.body->range.end;
  return result;
}

// // program = { const_decl } function { const_decl }
// EXACTLY ONE function. Zero or two is an error naming both locations.
// Consts may appear after the function, so collect them all, then hand the
// whole Program to sema — name resolution needs two passes over the file.
Program Parser::parseProgram() {
  Program program; bool haveFunction = false;
  while (!check(Tok::Eof)) {
    if (check(Tok::KwConst)) { program.consts.push_back(parseConstDecl()); continue; }
    if (check(Tok::Type)) {
      if (haveFunction) { diags_.error(peek().range, "only one function is allowed"); while (!check(Tok::Eof)) advance(); break; }
      program.fn = parseFunction(); haveFunction = true; continue;
    }
    diags_.error(peek().range, "expected a constant declaration or function"); advance();
  }
  if (!haveFunction) diags_.error(peek().range, "program must contain exactly one function");
  return program;
}

} // namespace minihls