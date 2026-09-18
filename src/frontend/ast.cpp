#include "frontend/ast.hpp"

#include <utility>

namespace minihls::ast {

TypePtr make_int_type(unsigned width, bool is_signed, SourceRange range) {
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Int;
  type->range = range;
  type->width = width;
  type->is_signed = is_signed;
  return type;
}

TypePtr make_array_type(unsigned width, bool is_signed, std::uint64_t size, bool is_const,
                        SourceRange range) {
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Array;
  type->range = range;
  type->width = width;
  type->is_signed = is_signed;
  type->array_size = size;
  type->is_const = is_const;
  return type;
}

TypePtr make_stream_type(unsigned width, bool is_signed, SourceRange range) {
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Stream;
  type->range = range;
  type->width = width;
  type->is_signed = is_signed;
  return type;
}

TypePtr make_void_type(SourceRange range) {
  auto type = std::make_unique<Type>();
  type->kind = TypeKind::Void;
  type->range = range;
  return type;
}

std::string type_to_string(const Type& type) {
  const std::string element =
      (type.is_signed ? "i" : "u") + std::to_string(type.width);
  switch (type.kind) {
    case TypeKind::Int:
      return element;
    case TypeKind::Array:
      return element + "[" + std::to_string(type.array_size) + "]";
    case TypeKind::Stream:
      return "stream<" + element + ">";
    case TypeKind::Void:
      return "void";
  }
  return element;
}

std::string_view unary_op_spelling(UnaryOp op) {
  switch (op) {
    case UnaryOp::Negate:
      return "-";
    case UnaryOp::BitNot:
      return "~";
    case UnaryOp::LogicalNot:
      return "!";
  }
  return "?";
}

std::string_view binary_op_spelling(BinaryOp op) {
  switch (op) {
    case BinaryOp::Add:
      return "+";
    case BinaryOp::Subtract:
      return "-";
    case BinaryOp::Multiply:
      return "*";
    case BinaryOp::Divide:
      return "/";
    case BinaryOp::Modulo:
      return "%";
    case BinaryOp::BitAnd:
      return "&";
    case BinaryOp::BitOr:
      return "|";
    case BinaryOp::BitXor:
      return "^";
    case BinaryOp::ShiftLeft:
      return "<<";
    case BinaryOp::ShiftRight:
      return ">>";
    case BinaryOp::Equal:
      return "==";
    case BinaryOp::NotEqual:
      return "!=";
    case BinaryOp::Less:
      return "<";
    case BinaryOp::LessEqual:
      return "<=";
    case BinaryOp::Greater:
      return ">";
    case BinaryOp::GreaterEqual:
      return ">=";
    case BinaryOp::LogicalAnd:
      return "&&";
    case BinaryOp::LogicalOr:
      return "||";
  }
  return "?";
}

int binary_op_precedence(BinaryOp op) {
  // Must match the table in LANGUAGE.md. Higher binds tighter.
  switch (op) {
    case BinaryOp::LogicalOr:
      return 2;
    case BinaryOp::LogicalAnd:
      return 3;
    case BinaryOp::BitOr:
      return 4;
    case BinaryOp::BitXor:
      return 5;
    case BinaryOp::BitAnd:
      return 6;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
      return 7;
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
      return 8;
    case BinaryOp::ShiftLeft:
    case BinaryOp::ShiftRight:
      return 9;
    case BinaryOp::Add:
    case BinaryOp::Subtract:
      return 10;
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
    case BinaryOp::Modulo:
      return 11;
  }
  return 0;
}

ExprPtr make_int_literal(std::uint64_t value, SourceRange range) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::IntLiteral;
  expr->range = range;
  expr->value = value;
  return expr;
}

ExprPtr make_name(std::string name, SourceRange range) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Name;
  expr->range = range;
  expr->name = std::move(name);
  return expr;
}

ExprPtr make_index(ExprPtr base, ExprPtr index, SourceRange range) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Index;
  expr->range = range;
  expr->lhs = std::move(base);
  expr->rhs = std::move(index);
  return expr;
}

ExprPtr make_call(std::string callee, std::vector<ExprPtr> args, SourceRange range) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Call;
  expr->range = range;
  expr->name = std::move(callee);
  expr->args = std::move(args);
  return expr;
}

ExprPtr make_unary(UnaryOp op, ExprPtr operand, SourceRange range) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Unary;
  expr->range = range;
  expr->unary_op = op;
  expr->lhs = std::move(operand);
  return expr;
}

ExprPtr make_binary(BinaryOp op, ExprPtr lhs, ExprPtr rhs, SourceRange range) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Binary;
  expr->range = range;
  expr->binary_op = op;
  expr->lhs = std::move(lhs);
  expr->rhs = std::move(rhs);
  return expr;
}

ExprPtr make_conditional(ExprPtr condition, ExprPtr then_value, ExprPtr else_value,
                         SourceRange range) {
  auto expr = std::make_unique<Expr>();
  expr->kind = ExprKind::Conditional;
  expr->range = range;
  expr->lhs = std::move(condition);
  expr->rhs = std::move(then_value);
  expr->third = std::move(else_value);
  return expr;
}

StmtPtr make_block(std::vector<StmtPtr> statements, SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Block;
  stmt->range = range;
  stmt->statements = std::move(statements);
  return stmt;
}

StmtPtr make_var_decl(TypePtr type, std::string name, std::vector<ExprPtr> init,
                      bool init_is_list, SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::VarDecl;
  stmt->range = range;
  stmt->type = std::move(type);
  stmt->name = std::move(name);
  stmt->init = std::move(init);
  stmt->init_is_list = init_is_list;
  return stmt;
}

StmtPtr make_assign(ExprPtr target, ExprPtr value, SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Assign;
  stmt->range = range;
  stmt->target = std::move(target);
  stmt->value = std::move(value);
  return stmt;
}

StmtPtr make_call_stmt(ExprPtr call, SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Call;
  stmt->range = range;
  stmt->value = std::move(call);
  return stmt;
}

StmtPtr make_if(ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch,
                SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::If;
  stmt->range = range;
  stmt->value = std::move(condition);
  stmt->then_branch = std::move(then_branch);
  stmt->else_branch = std::move(else_branch);
  return stmt;
}

StmtPtr make_for(StmtPtr init, ExprPtr condition, StmtPtr step, StmtPtr body,
                 SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::For;
  stmt->range = range;
  stmt->for_init = std::move(init);
  stmt->value = std::move(condition);
  stmt->for_step = std::move(step);
  stmt->body = std::move(body);
  return stmt;
}

StmtPtr make_return(ExprPtr value, SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Return;
  stmt->range = range;
  stmt->value = std::move(value);
  return stmt;
}

StmtPtr make_pragma(Pragma pragma, SourceRange range) {
  auto stmt = std::make_unique<Stmt>();
  stmt->kind = StmtKind::Pragma;
  stmt->range = range;
  stmt->pragma = std::move(pragma);
  return stmt;
}

}  // namespace minihls::ast
