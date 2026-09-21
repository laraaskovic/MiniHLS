#pragma once

#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"
#include <deque>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace minihls {

class ConstantEvaluator {
public:
  explicit ConstantEvaluator(Diagnostics& diags) : diags_(diags) {}

  void evaluate(Program& program);

private:
  std::optional<Bits> eval(Expr& expr, bool diagnose = true);
  std::optional<Bits> evalConstant(Symbol* symbol, bool diagnose);
  void evaluateConst(ConstDecl& decl);
  void evaluateBlock(Block& block);
  void evaluateStmt(Stmt& stmt);
  void validateArray(Expr* size, Symbol* symbol, Range range);
  void validateIndex(Expr& expr);
  void evaluateFor(For& loop);
  bool fitsType(Bits value, Type type) const;
  bool valueAsI128(Bits value, i128& result) const;
  void report(Range range, const std::string& message);

  std::unordered_map<Symbol*, ConstDecl*> constants_;
  std::unordered_map<Symbol*, Bits> values_;
  std::unordered_set<Symbol*> inProgress_;
  Diagnostics& diags_;
};

} // namespace minihls