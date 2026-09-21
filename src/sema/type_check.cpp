#include "sema/type_check.hpp"
#include "sema/symbol.hpp"

#include <algorithm>
#include <string>

namespace minihls {
namespace {

std::string typeName(Type type) {
  if (type.isPoly) return "untyped constant";
  return std::string(type.isSigned ? "i" : "u") + std::to_string(type.width);
}

} // namespace

void TypeChecker::report(Range range, const std::string& message) {
  diags_.error(range, message);
}

bool TypeChecker::fits(u128 value, Type type) const {
  if (type.isSigned) {
    if (type.width == 128) return value <= (static_cast<u128>(1) << 127) - 1;
    return value <= ((static_cast<u128>(1) << (type.width - 1)) - 1);
  }
  return type.width == 128 || value <= ((static_cast<u128>(1) << type.width) - 1);
}

bool TypeChecker::assignable(Type from, Type to) const {
  return !from.isPoly && from.isSigned == to.isSigned && from.width <= to.width;
}

Type TypeChecker::widthRule(Tok op, Type left, Type right, Range range) {
  Type result;
  unsigned maxWidth = std::max(left.width, right.width);
  switch (op) {
    case Tok::Plus: result = {maxWidth + 1, left.isSigned}; break;
    case Tok::Minus: result = {maxWidth + 1, true}; break;
    case Tok::Star: result = {left.width + right.width, left.isSigned}; break;
    case Tok::Slash: result = left.isSigned ? Type{left.width + 1, true}
                                            : Type{left.width, false}; break;
    case Tok::Percent: result = {std::min(left.width, right.width), left.isSigned}; break;
    case Tok::Amp: case Tok::Pipe: case Tok::Caret:
      result = {maxWidth, left.isSigned}; break;
    case Tok::Shl: case Tok::Shr: result = {left.width, left.isSigned}; break;
    case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge:
    case Tok::EqEq: case Tok::BangEq: case Tok::AmpAmp: case Tok::PipePipe:
      result = {1, false}; break;
    default:
      report(range, "invalid binary operator");
      return {};
  }
  if (result.width > 128)
    report(range, "expression result exceeds 128 bits");
  return result;
}

Type TypeChecker::infer(Expr& expr) {
  if (expr.typeKnown) return expr.type;
  Type result;
  switch (expr.kind) {
    case ExprKind::IntLit:
      result.isPoly = true;
      result.constValue = static_cast<IntLit&>(expr).value;
      break;
    case ExprKind::NameRef: {
      auto& node = static_cast<NameRef&>(expr);
      if (!node.symbol) { report(expr.range, "unresolved name"); break; }
      result = node.symbol->type;
      break;
    }
    case ExprKind::Index: {
      auto& node = static_cast<Index&>(expr);
      if (node.index) infer(*node.index);
      if (node.symbol) result = node.symbol->type;
      break;
    }
    case ExprKind::Cast: {
      auto& node = static_cast<Cast&>(expr);
      infer(*node.operand);
      result = node.type;
      break;
    }
    case ExprKind::Unary: {
      auto& node = static_cast<Unary&>(expr);
      Type operand = infer(*node.operand);
      if (operand.isPoly) result = operand;
      else if (node.op == Tok::Minus) result = {operand.width + 1, true};
      else result = operand;
      break;
    }
    case ExprKind::Binary: {
      auto& node = static_cast<Binary&>(expr);
      Type left = infer(*node.lhs);
      Type right = infer(*node.rhs);
      if (left.isPoly && right.isPoly) {
        result.isPoly = true;
        if (node.op == Tok::Plus) result.constValue = left.constValue + right.constValue;
        else if (node.op == Tok::Star) result.constValue = left.constValue * right.constValue;
        else result.constValue = left.constValue;
      } else {
        if (left.isPoly) { checkExpr(*node.lhs, right); left = right; }
        if (right.isPoly) { checkExpr(*node.rhs, left); right = left; }
        if (left.isSigned != right.isSigned)
          report(expr.range, "mixed signedness: operands are " + typeName(left) + " and " + typeName(right));
        result = widthRule(node.op, left, right, expr.range);
      }
      break;
    }
    case ExprKind::Ternary: {
      auto& node = static_cast<Ternary&>(expr);
      checkExpr(*node.cond, Type{1, false});
      Type left = infer(*node.thenE), right = infer(*node.elseE);
      if (left.isPoly && !right.isPoly) checkExpr(*node.thenE, right), left = right;
      if (right.isPoly && !left.isPoly) checkExpr(*node.elseE, left), right = left;
      if (!left.isPoly && !right.isPoly && (left.isSigned != right.isSigned || left.width != right.width))
        report(expr.range, "conditional arms have different types");
      result = left.isPoly ? right : left;
      break;
    }
  }
  expr.type = result;
  expr.typeKnown = true;
  return result;
}

void TypeChecker::checkExpr(Expr& expr, Type expected) {
  Type actual = infer(expr);
  if (actual.isPoly) {
    if (!fits(actual.constValue, expected))
      report(expr.range, "constant does not fit " + typeName(expected));
    expr.type = expected;
    expr.typeKnown = true;
    return;
  }
  if (!assignable(actual, expected))
    report(expr.range, "cannot use " + typeName(actual) + " where " + typeName(expected) + " is required");
}

void TypeChecker::checkBlock(Block& block, Type returnType) {
  for (auto& statement : block.stmts) checkStmt(*statement, returnType);
}

void TypeChecker::checkStmt(Stmt& stmt, Type returnType) {
  switch (stmt.kind) {
    case StmtKind::VarDecl: {
      auto& node = static_cast<VarDecl&>(stmt);
      if (node.init) checkExpr(*node.init, node.type);
      if (node.initIsRead && node.readSymbol &&
          node.readSymbol->type.width != node.type.width)
        report(node.range, "stream value type does not match declaration type");
      return;
    }
    case StmtKind::ArrayDecl: {
      auto& node = static_cast<ArrayDecl&>(stmt);
      for (auto& value : node.init) checkExpr(*value, node.elem);
      return;
    }
    case StmtKind::Assign: {
      auto& node = static_cast<Assign&>(stmt);
      Type target = node.symbol ? node.symbol->type : Type{};
      if (node.value) checkExpr(*node.value, target);
      return;
    }
    case StmtKind::Write: {
      auto& node = static_cast<Write&>(stmt);
      if (node.value && node.symbol) checkExpr(*node.value, node.symbol->type);
      return;
    }
    case StmtKind::If: {
      auto& node = static_cast<If&>(stmt);
      checkExpr(*node.cond, Type{1, false});
      checkBlock(*node.thenB, returnType);
      if (node.elseS) checkStmt(*node.elseS, returnType);
      return;
    }
    case StmtKind::For: {
      auto& node = static_cast<For&>(stmt);
      if (node.ivSymbol) {
        checkExpr(*node.init, node.ivType);
        checkExpr(*node.limit, node.ivType);
        checkExpr(*node.step, node.ivType);
      }
      checkBlock(*node.body, returnType);
      return;
    }
    case StmtKind::Return:
      checkExpr(*static_cast<Return&>(stmt).value, returnType); return;
    case StmtKind::Block:
      checkBlock(static_cast<Block&>(stmt), returnType); return;
  }
}

void TypeChecker::check(Program& program) {
  for (auto& constant : program.consts) {
    if (constant.init) checkExpr(*constant.init, constant.type);
    for (auto& value : constant.arrayInit) checkExpr(*value, constant.type);
  }
  for (auto& param : program.fn.params)
    if (param.size) infer(*param.size);
  if (program.fn.body) checkBlock(*program.fn.body, program.fn.returnType);
}

} // namespace minihls