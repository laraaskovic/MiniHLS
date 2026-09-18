#include "frontend/parser.hpp"

#include "frontend/lexer.hpp"

#include <utility>

namespace minihls {
namespace {

using namespace minihls::ast;

// Lowest precedence any binary operator has; see binary_op_precedence().
constexpr int kLowestBinaryPrecedence = 2;

bool token_to_binary_op(TokenKind kind, BinaryOp& op) {
  switch (kind) {
    case TokenKind::PipePipe:
      op = BinaryOp::LogicalOr;
      return true;
    case TokenKind::AmpAmp:
      op = BinaryOp::LogicalAnd;
      return true;
    case TokenKind::Pipe:
      op = BinaryOp::BitOr;
      return true;
    case TokenKind::Caret:
      op = BinaryOp::BitXor;
      return true;
    case TokenKind::Amp:
      op = BinaryOp::BitAnd;
      return true;
    case TokenKind::EqualEqual:
      op = BinaryOp::Equal;
      return true;
    case TokenKind::BangEqual:
      op = BinaryOp::NotEqual;
      return true;
    case TokenKind::Less:
      op = BinaryOp::Less;
      return true;
    case TokenKind::LessEqual:
      op = BinaryOp::LessEqual;
      return true;
    case TokenKind::Greater:
      op = BinaryOp::Greater;
      return true;
    case TokenKind::GreaterEqual:
      op = BinaryOp::GreaterEqual;
      return true;
    case TokenKind::LessLess:
      op = BinaryOp::ShiftLeft;
      return true;
    case TokenKind::GreaterGreater:
      op = BinaryOp::ShiftRight;
      return true;
    case TokenKind::Plus:
      op = BinaryOp::Add;
      return true;
    case TokenKind::Minus:
      op = BinaryOp::Subtract;
      return true;
    case TokenKind::Star:
      op = BinaryOp::Multiply;
      return true;
    case TokenKind::Slash:
      op = BinaryOp::Divide;
      return true;
    case TokenKind::Percent:
      op = BinaryOp::Modulo;
      return true;
    default:
      return false;
  }
}

// Tokens that can begin a declaration, and therefore a statement.
bool starts_declaration(TokenKind kind) {
  return kind == TokenKind::IntType || kind == TokenKind::KwConst;
}

}  // namespace

Parser::Parser(const SourceFile& file, std::vector<Token> tokens,
               DiagnosticEngine& diagnostics)
    : file_(&file), tokens_(std::move(tokens)), diagnostics_(&diagnostics) {}

const Token& Parser::peek(std::size_t ahead) const {
  const std::size_t index = position_ + ahead;
  // The token vector always ends with EndOfFile, so clamping is safe.
  return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
}

bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::check_ahead(std::size_t ahead, TokenKind kind) const {
  return peek(ahead).kind == kind;
}

bool Parser::at_end() const { return check(TokenKind::EndOfFile); }

const Token& Parser::advance() {
  const Token& token = peek();
  if (!at_end()) ++position_;
  return token;
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) return false;
  advance();
  return true;
}

std::string Parser::describe_token(const Token& token) const {
  switch (token.kind) {
    case TokenKind::Identifier:
    case TokenKind::IntLiteral:
    case TokenKind::IntType:
      return "'" + std::string(file_->text_for(token.range)) + "'";
    default:
      return std::string(describe(token.kind));
  }
}

const Token& Parser::expect(TokenKind kind, std::string_view context) {
  if (check(kind)) return advance();

  std::string message = "expected ";
  message += describe(kind);
  message += ' ';
  message += context;
  message += ", but found ";
  message += describe_token(peek());

  // A missing separator reads better anchored just past the previous token than
  // pointing at whatever turned up next -- which is often on the following line
  // and has nothing to do with the mistake.
  if (kind == TokenKind::Semicolon && position_ > 0) {
    const SourceLocation after = tokens_[position_ - 1].range.end;
    fail(SourceRange{after, after}, std::move(message));
  }
  fail(peek().range, std::move(message));
}

void Parser::fail(SourceRange range, std::string message) {
  diagnostics_->error(range, std::move(message));
  throw ParseError{};
}

void Parser::fail_here(std::string message) { fail(peek().range, std::move(message)); }

void Parser::synchronize_to_statement() {
  // Stop after a semicolon (the statement is over) or before a brace (the
  // block is over). Always consume something so recovery cannot spin.
  while (!at_end()) {
    if (check(TokenKind::RBrace)) return;
    const TokenKind kind = advance().kind;
    if (kind == TokenKind::Semicolon) return;
    if (check(TokenKind::LBrace)) return;
  }
}

void Parser::synchronize_to_function() {
  while (!at_end()) {
    if (check(TokenKind::IntType) || check(TokenKind::KwVoid)) return;
    advance();
  }
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

std::unique_ptr<ast::Program> Parser::parse_program() {
  auto program = std::make_unique<Program>();
  while (!at_end()) {
    const std::size_t before = position_;
    try {
      program->functions.push_back(parse_function());
    } catch (const ParseError&) {
      synchronize_to_function();
      // If recovery made no progress we would loop forever on the same token.
      if (position_ == before) advance();
    }
  }
  return program;
}

ast::TypePtr Parser::parse_int_type() {
  const Token& token = expect(TokenKind::IntType, "in type position");
  return make_int_type(token.width, token.is_signed, token.range);
}

ast::TypePtr Parser::parse_return_type() {
  if (check(TokenKind::KwVoid)) {
    const Token& token = advance();
    return make_void_type(token.range);
  }
  if (check(TokenKind::IntType)) return parse_int_type();
  fail_here("expected a return type such as 'i32' or 'void', but found " +
            describe_token(peek()));
}

ast::Param Parser::parse_param() {
  Param param;

  if (check(TokenKind::KwStream)) {
    const Token& stream_token = advance();
    expect(TokenKind::Less, "after 'stream'");
    const Token& element = expect(TokenKind::IntType, "as the stream element type");
    expect(TokenKind::Greater, "to close 'stream<...>'");
    const Token& name = expect(TokenKind::Identifier, "as the parameter name");
    param.type = make_stream_type(element.width, element.is_signed,
                                  join(stream_token.range, name.range));
    param.name = std::string(file_->text_for(name.range));
    param.range = join(stream_token.range, name.range);
    return param;
  }

  const bool is_const = check(TokenKind::KwConst);
  const SourceRange start = peek().range;
  if (is_const) advance();

  const Token& element = expect(TokenKind::IntType, "as the parameter type");
  const Token& name = expect(TokenKind::Identifier, "as the parameter name");
  param.name = std::string(file_->text_for(name.range));

  if (match(TokenKind::LBracket)) {
    const Token& size = expect(TokenKind::IntLiteral, "as the array length");
    const Token& close = expect(TokenKind::RBracket, "to close the array length");
    if (size.value == 0) {
      fail(size.range, "array length must be at least 1");
    }
    param.type = make_array_type(element.width, element.is_signed, size.value, is_const,
                                 join(start, close.range));
    param.range = join(start, close.range);
    return param;
  }

  if (is_const) {
    fail(start, "'const' applies only to array parameters, which become ROMs");
  }
  param.type = make_int_type(element.width, element.is_signed, element.range);
  param.range = join(start, name.range);
  return param;
}

std::unique_ptr<ast::Function> Parser::parse_function() {
  auto function = std::make_unique<Function>();
  const SourceRange start = peek().range;

  function->return_type = parse_return_type();
  const Token& name = expect(TokenKind::Identifier, "as the function name");
  function->name = std::string(file_->text_for(name.range));

  expect(TokenKind::LParen, "after the function name");
  if (!check(TokenKind::RParen)) {
    do {
      function->params.push_back(parse_param());
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RParen, "to close the parameter list");

  function->body = parse_block();
  function->range = join(start, function->body->range);
  return function;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

ast::StmtPtr Parser::parse_block() {
  const Token& open = expect(TokenKind::LBrace, "to open a block");
  std::vector<StmtPtr> statements;
  while (!check(TokenKind::RBrace) && !at_end()) {
    const std::size_t before = position_;
    try {
      statements.push_back(parse_statement());
    } catch (const ParseError&) {
      synchronize_to_statement();
      if (position_ == before) advance();
    }
  }
  const Token& close = expect(TokenKind::RBrace, "to close a block");
  return make_block(std::move(statements), join(open.range, close.range));
}

ast::StmtPtr Parser::parse_statement() {
  if (check(TokenKind::LBrace)) return parse_block();
  if (check(TokenKind::KwIf)) return parse_if();
  if (check(TokenKind::KwFor)) return parse_for();
  if (check(TokenKind::KwReturn)) return parse_return();
  if (check(TokenKind::Hash)) return parse_pragma();
  if (starts_declaration(peek().kind)) return parse_var_decl(/*consume_semicolon=*/true);

  if (check(TokenKind::Identifier)) {
    // `name(` is a call statement; anything else beginning with a name must be
    // an assignment.
    if (check_ahead(1, TokenKind::LParen)) return parse_call_statement();
    return parse_assignment(/*consume_semicolon=*/true);
  }

  if (check(TokenKind::Semicolon)) {
    fail_here("empty statement; remove the stray ';'");
  }
  fail_here("expected a statement, but found " + describe_token(peek()));
}

ast::StmtPtr Parser::parse_var_decl(bool consume_semicolon) {
  const SourceRange start = peek().range;
  const bool is_const = match(TokenKind::KwConst);
  const Token& element = expect(TokenKind::IntType, "in a declaration");
  const Token& name = expect(TokenKind::Identifier, "as the declared name");
  std::string declared_name(file_->text_for(name.range));

  TypePtr type;
  SourceRange type_range = join(start, name.range);
  bool is_array = false;
  if (match(TokenKind::LBracket)) {
    const Token& size = expect(TokenKind::IntLiteral, "as the array length");
    const Token& close = expect(TokenKind::RBracket, "to close the array length");
    if (size.value == 0) fail(size.range, "array length must be at least 1");
    type_range = join(start, close.range);
    type = make_array_type(element.width, element.is_signed, size.value, is_const,
                           type_range);
    is_array = true;
  } else {
    if (is_const) {
      fail(start, "'const' applies only to arrays, which become ROMs");
    }
    type = make_int_type(element.width, element.is_signed, element.range);
  }

  std::vector<ExprPtr> init;
  bool init_is_list = false;
  if (match(TokenKind::Assign)) {
    if (check(TokenKind::LBrace)) {
      if (!is_array) {
        fail_here("a braced initializer is only valid for an array");
      }
      advance();
      init_is_list = true;
      if (!check(TokenKind::RBrace)) {
        do {
          // Allow a trailing comma before the closing brace.
          if (check(TokenKind::RBrace)) break;
          init.push_back(parse_expression());
        } while (match(TokenKind::Comma));
      }
      expect(TokenKind::RBrace, "to close the initializer list");
    } else {
      if (is_array) {
        fail_here("an array must be initialized with a braced list");
      }
      init.push_back(parse_expression());
    }
  } else if (is_const) {
    fail(type_range, "a 'const' array must have an initializer");
  }

  SourceRange range = type_range;
  if (consume_semicolon) {
    const Token& semi = expect(TokenKind::Semicolon, "after a declaration");
    range = join(range, semi.range);
  }
  return make_var_decl(std::move(type), std::move(declared_name), std::move(init),
                       init_is_list, range);
}

ast::ExprPtr Parser::parse_lvalue() {
  const Token& name = expect(TokenKind::Identifier, "as an assignment target");
  ExprPtr target = make_name(std::string(file_->text_for(name.range)), name.range);
  if (match(TokenKind::LBracket)) {
    ExprPtr index = parse_expression();
    const Token& close = expect(TokenKind::RBracket, "to close an array index");
    target = make_index(std::move(target), std::move(index), join(name.range, close.range));
  }
  return target;
}

ast::StmtPtr Parser::parse_assignment(bool consume_semicolon) {
  ExprPtr target = parse_lvalue();
  const SourceRange start = target->range;
  expect(TokenKind::Assign, "in an assignment");
  ExprPtr value = parse_expression();
  SourceRange range = join(start, value->range);
  if (consume_semicolon) {
    const Token& semi = expect(TokenKind::Semicolon, "after an assignment");
    range = join(range, semi.range);
  }
  return make_assign(std::move(target), std::move(value), range);
}

ast::StmtPtr Parser::parse_call_statement() {
  ExprPtr call = parse_expression();
  if (call->kind != ExprKind::Call) {
    fail(call->range, "only a call may be used as a statement");
  }
  const Token& semi = expect(TokenKind::Semicolon, "after a call statement");
  // The range must be computed before the pointer is moved: argument evaluation
  // order is unspecified, so reading `call->range` in the same call expression
  // can happen after `call` has already been emptied.
  const SourceRange range = join(call->range, semi.range);
  return make_call_stmt(std::move(call), range);
}

ast::StmtPtr Parser::parse_if() {
  const Token& keyword = expect(TokenKind::KwIf, "to begin a conditional");
  expect(TokenKind::LParen, "after 'if'");
  ExprPtr condition = parse_expression();
  expect(TokenKind::RParen, "to close the 'if' condition");
  StmtPtr then_branch = parse_statement();
  StmtPtr else_branch;
  if (match(TokenKind::KwElse)) else_branch = parse_statement();

  const SourceRange end = else_branch ? else_branch->range : then_branch->range;
  return make_if(std::move(condition), std::move(then_branch), std::move(else_branch),
                 join(keyword.range, end));
}

ast::StmtPtr Parser::parse_for() {
  const Token& keyword = expect(TokenKind::KwFor, "to begin a loop");
  expect(TokenKind::LParen, "after 'for'");

  StmtPtr init;
  if (starts_declaration(peek().kind)) {
    init = parse_var_decl(/*consume_semicolon=*/false);
  } else {
    init = parse_assignment(/*consume_semicolon=*/false);
  }
  expect(TokenKind::Semicolon, "after the loop initializer");

  ExprPtr condition = parse_expression();
  expect(TokenKind::Semicolon, "after the loop condition");

  StmtPtr step = parse_assignment(/*consume_semicolon=*/false);
  expect(TokenKind::RParen, "to close the loop header");

  StmtPtr body = parse_statement();
  const SourceRange range = join(keyword.range, body->range);
  return make_for(std::move(init), std::move(condition), std::move(step), std::move(body),
                  range);
}

ast::StmtPtr Parser::parse_return() {
  const Token& keyword = expect(TokenKind::KwReturn, "to begin a return statement");
  ExprPtr value;
  if (!check(TokenKind::Semicolon)) value = parse_expression();
  const Token& semi = expect(TokenKind::Semicolon, "after a return statement");
  return make_return(std::move(value), join(keyword.range, semi.range));
}

const Token& Parser::parse_named_integer(std::string_view expected_name,
                                         std::string_view context) {
  const Token& name = expect(TokenKind::Identifier, context);
  if (file_->text_for(name.range) != expected_name) {
    fail(name.range, "expected '" + std::string(expected_name) + "', but found " +
                         describe_token(name));
  }
  expect(TokenKind::Assign, "after '" + std::string(expected_name) + "'");
  return expect(TokenKind::IntLiteral,
                "as the value of '" + std::string(expected_name) + "'");
}

ast::StmtPtr Parser::parse_pragma() {
  const Token& hash = expect(TokenKind::Hash, "to begin a pragma");
  expect(TokenKind::KwPragma, "after '#'");
  const Token& name = expect(TokenKind::Identifier, "as the pragma name");
  const std::string_view text = file_->text_for(name.range);

  Pragma pragma;
  SourceRange range = join(hash.range, name.range);

  if (text == "pipeline") {
    pragma.kind = PragmaKind::Pipeline;
    const Token& value = parse_named_integer("II", "in '#pragma pipeline'");
    if (value.value == 0) {
      fail(value.range, "initiation interval must be at least 1");
    }
    pragma.initiation_interval = value.value;
    range = join(range, value.range);
  } else if (text == "unroll") {
    pragma.kind = PragmaKind::Unroll;
    // `factor` is optional; without it the loop is unrolled completely.
    if (check(TokenKind::Identifier) && file_->text_for(peek().range) == "factor") {
      const Token& value = parse_named_integer("factor", "in '#pragma unroll'");
      if (value.value == 0) fail(value.range, "unroll factor must be at least 1");
      pragma.factor = value.value;
      range = join(range, value.range);
    }
  } else if (text == "partition") {
    pragma.kind = PragmaKind::Partition;
    const Token& array = expect(TokenKind::Identifier,
                                "as the array to partition in '#pragma partition'");
    pragma.array_name = std::string(file_->text_for(array.range));
    range = join(range, array.range);
    if (check(TokenKind::Identifier) && file_->text_for(peek().range) == "factor") {
      const Token& value = parse_named_integer("factor", "in '#pragma partition'");
      if (value.value == 0) fail(value.range, "partition factor must be at least 1");
      pragma.factor = value.value;
      range = join(range, value.range);
    }
  } else {
    fail(name.range, "unknown pragma " + describe_token(name) +
                         "; expected 'pipeline', 'unroll', or 'partition'");
  }

  pragma.range = range;
  return make_pragma(std::move(pragma), range);
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

ast::ExprPtr Parser::parse_expression() { return parse_conditional(); }

ast::ExprPtr Parser::parse_conditional() {
  ExprPtr condition = parse_binary(kLowestBinaryPrecedence);
  if (!match(TokenKind::Question)) return condition;

  // The middle arm is a full expression; the right arm recurses into
  // parse_conditional so that `?:` is right-associative.
  ExprPtr then_value = parse_expression();
  expect(TokenKind::Colon, "in a conditional expression");
  ExprPtr else_value = parse_conditional();
  const SourceRange range = join(condition->range, else_value->range);
  return make_conditional(std::move(condition), std::move(then_value),
                          std::move(else_value), range);
}

ast::ExprPtr Parser::parse_binary(int min_precedence) {
  ExprPtr lhs = parse_unary();
  while (true) {
    BinaryOp op = BinaryOp::Add;
    if (!token_to_binary_op(peek().kind, op)) break;
    const int precedence = binary_op_precedence(op);
    if (precedence < min_precedence) break;
    advance();
    // Every binary operator in this language is left-associative, so the
    // right-hand side must bind strictly tighter.
    ExprPtr rhs = parse_binary(precedence + 1);
    const SourceRange range = join(lhs->range, rhs->range);
    lhs = make_binary(op, std::move(lhs), std::move(rhs), range);
  }
  return lhs;
}

ast::ExprPtr Parser::parse_unary() {
  UnaryOp op = UnaryOp::Negate;
  bool is_unary = true;
  if (check(TokenKind::Minus)) {
    op = UnaryOp::Negate;
  } else if (check(TokenKind::Tilde)) {
    op = UnaryOp::BitNot;
  } else if (check(TokenKind::Bang)) {
    op = UnaryOp::LogicalNot;
  } else {
    is_unary = false;
  }

  if (!is_unary) return parse_postfix();

  const Token& token = advance();
  ExprPtr operand = parse_unary();
  const SourceRange range = join(token.range, operand->range);
  return make_unary(op, std::move(operand), range);
}

ast::ExprPtr Parser::parse_postfix() {
  ExprPtr expr = parse_primary();
  while (true) {
    if (match(TokenKind::LBracket)) {
      ExprPtr index = parse_expression();
      const Token& close = expect(TokenKind::RBracket, "to close an array index");
      const SourceRange range = join(expr->range, close.range);
      expr = make_index(std::move(expr), std::move(index), range);
      continue;
    }
    if (check(TokenKind::LParen)) {
      if (expr->kind != ExprKind::Name) {
        fail(expr->range, "only a named function may be called");
      }
      advance();
      std::vector<ExprPtr> args;
      if (!check(TokenKind::RParen)) {
        do {
          args.push_back(parse_expression());
        } while (match(TokenKind::Comma));
      }
      const Token& close = expect(TokenKind::RParen, "to close an argument list");
      const SourceRange range = join(expr->range, close.range);
      expr = make_call(expr->name, std::move(args), range);
      continue;
    }
    break;
  }
  return expr;
}

ast::ExprPtr Parser::parse_primary() {
  if (check(TokenKind::IntLiteral)) {
    const Token& token = advance();
    return make_int_literal(token.value, token.range);
  }
  if (check(TokenKind::Identifier)) {
    const Token& token = advance();
    return make_name(std::string(file_->text_for(token.range)), token.range);
  }
  if (match(TokenKind::LParen)) {
    ExprPtr inner = parse_expression();
    expect(TokenKind::RParen, "to close a parenthesized expression");
    return inner;
  }
  fail_here("expected an expression, but found " + describe_token(peek()));
}

// ---------------------------------------------------------------------------

std::unique_ptr<ast::Program> parse(const SourceFile& file, DiagnosticEngine& diagnostics) {
  Lexer lexer(file, diagnostics);
  Parser parser(file, lexer.tokenize(), diagnostics);
  return parser.parse_program();
}

}  // namespace minihls
