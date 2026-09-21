#pragma once

#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"

namespace minihls {

class TypeChecker {
public:
  explicit TypeChecker(Diagnostics& diags) : diags_(diags) {}

  void check(Program& program);

private:
  Type infer(Expr& expr);
  void checkExpr(Expr& expr, Type expected);
  Type widthRule(Tok op, Type left, Type right, Range range);
  bool assignable(Type from, Type to) const;
  bool fits(u128 value, Type type) const;
  void report(Range range, const std::string& message);
  void checkStmt(Stmt& stmt, Type returnType);
  void checkBlock(Block& block, Type returnType);

  Diagnostics& diags_;
};

} // namespace minihls