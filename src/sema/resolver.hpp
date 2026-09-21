#pragma once

#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"
#include "sema/scope.hpp"
#include <deque>

namespace minihls {

class Resolver {
public:
  explicit Resolver(Diagnostics& diags) : diags_(diags) {}

  void resolve(Program& program);

private:
  Symbol* newSymbol(SymbolKind kind, std::string name, Type type, Range range);
  Symbol* declare(SymbolKind kind, const std::string& name, Type type,
                 Range range, bool isStreamOut = false,
                 uint64_t arrayLength = 0);
  void report(Range range, const std::string& message);

  void resolveConst(ConstDecl& decl);
  void resolveFunction(Function& function);
  void resolveParam(const Param& param);
  void resolveBlock(Block& block, bool createScope = true);
  void resolveStmt(Stmt& stmt);
  void resolveExpr(Expr& expr);
  Symbol* lookup(const std::string& name, Range useRange);
  Symbol* lookupStream(const std::string& name, Range useRange,
                       bool writing);

  std::deque<Symbol> symbols_;
  ScopeStack scopes_;
  Diagnostics& diags_;
};

} // namespace minihls