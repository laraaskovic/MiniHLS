#include "sema/resolver.hpp"

namespace minihls {

Symbol* Resolver::newSymbol(SymbolKind kind, std::string name, Type type,
                            Range range) {
  // Keep symbols in stable storage because AST nodes store their addresses.
  symbols_.push_back(Symbol{kind, std::move(name), type, range});
  return &symbols_.back();
}

void Resolver::report(Range range, const std::string& message) {
  // Send semantic errors through the shared source diagnostic system.
  diags_.error(range, message);
}

Symbol* Resolver::declare(SymbolKind kind, const std::string& name, Type type,
                          Range range, bool isStreamOut, uint64_t arrayLength) {
  // Create the declaration before checking whether its name is already used.
  Symbol* symbol = newSymbol(kind, name, type, range);
  symbol->isStreamOut = isStreamOut;
  symbol->arrayLength = arrayLength;
  if (scopes_.declare(symbol)) return symbol;

  // Report both the new declaration and the earlier declaration.
  Symbol* previous = scopes_.lookup(name);
  report(range, "redeclaration of '" + name + "'");
  if (previous) {
    report(previous->declRange, "previous declaration of '" + name + "' is here");
  }
  return nullptr;
}

Symbol* Resolver::lookup(const std::string& name, Range useRange) {
  // Search from the current scope outward so inner names shadow outer names.
  Symbol* symbol = scopes_.lookup(name);
  if (!symbol) report(useRange, "unknown name '" + name + "'");
  return symbol;
}

Symbol* Resolver::lookupStream(const std::string& name, Range useRange,
                               bool writing) {
  // Resolve the stream first, then check its direction.
  Symbol* symbol = lookup(name, useRange);
  if (!symbol) return nullptr;
  if (symbol->kind != SymbolKind::StreamParam) {
    report(useRange, "'" + name + "' is not a stream");
    return nullptr;
  }
  if (writing && !symbol->isStreamOut)
    report(useRange, "cannot write to input stream '" + name + "'");
  if (!writing && symbol->isStreamOut)
    report(useRange, "cannot read from output stream '" + name + "'");
  return symbol;
}

void Resolver::resolve(Program& program) {
  // Declare globals first so constants may appear after the function.
  scopes_.push();
  for (ConstDecl& decl : program.consts) {
    SymbolKind kind = decl.isArray ? SymbolKind::GlobalConstArray
                                   : SymbolKind::GlobalConst;
    declare(kind, decl.name, decl.type, decl.range);
  }
  for (ConstDecl& decl : program.consts) resolveConst(decl);
  // Resolve the function after every global name is available.
  resolveFunction(program.fn);
  scopes_.pop();
}

void Resolver::resolveConst(ConstDecl& decl) {
  // Resolve names used by constant sizes and initializer expressions.
  if (decl.size) resolveExpr(*decl.size);
  if (decl.init) resolveExpr(*decl.init);
  for (auto& value : decl.arrayInit) resolveExpr(*value);
}

void Resolver::resolveParam(const Param& param) {
  // Convert the parameter syntax into the matching symbol category.
  SymbolKind kind = SymbolKind::ScalarParam;
  bool streamOut = false;
  if (param.kind == ParamKind::Array) kind = SymbolKind::ArrayParam;
  if (param.kind == ParamKind::StreamIn || param.kind == ParamKind::StreamOut) {
    kind = SymbolKind::StreamParam;
    streamOut = param.kind == ParamKind::StreamOut;
  }
  declare(kind, param.name, param.type, param.range, streamOut);
}

void Resolver::resolveFunction(Function& function) {
  // Parameters and locals belong to the function scope.
  for (const Param& param : function.params)
    if (param.size) resolveExpr(*param.size);
  scopes_.push();
  for (const Param& param : function.params) resolveParam(param);
  if (function.body) resolveBlock(*function.body, false);
  scopes_.pop();
}

void Resolver::resolveBlock(Block& block, bool createScope) {
  // The function body already has a scope, but nested blocks need one.
  if (createScope) scopes_.push();
  for (auto& statement : block.stmts) resolveStmt(*statement);
  if (createScope) scopes_.pop();
}

void Resolver::resolveExpr(Expr& expr) {
  // Walk children and attach symbols wherever an expression names something.
  switch (expr.kind) {
    case ExprKind::IntLit:
      return;
    case ExprKind::NameRef: {
      auto& node = static_cast<NameRef&>(expr);
      node.symbol = lookup(node.name, node.range);
      return;
    }
    case ExprKind::Index: {
      auto& node = static_cast<Index&>(expr);
      // The base name must resolve to an array declaration.
      node.symbol = lookup(node.array, node.range);
      if (node.symbol && node.symbol->kind != SymbolKind::ArrayParam &&
          node.symbol->kind != SymbolKind::LocalArray &&
          node.symbol->kind != SymbolKind::GlobalConstArray) {
        report(node.range, "'" + node.array + "' is not an array");
      }
      if (node.index) resolveExpr(*node.index);
      return;
    }
    case ExprKind::Cast:
      resolveExpr(*static_cast<Cast&>(expr).operand);
      return;
    case ExprKind::Unary:
      resolveExpr(*static_cast<Unary&>(expr).operand);
      return;
    case ExprKind::Binary: {
      auto& node = static_cast<Binary&>(expr);
      resolveExpr(*node.lhs);
      resolveExpr(*node.rhs);
      return;
    }
    case ExprKind::Ternary: {
      auto& node = static_cast<Ternary&>(expr);
      resolveExpr(*node.cond);
      resolveExpr(*node.thenE);
      resolveExpr(*node.elseE);
      return;
    }
  }
}

void Resolver::resolveStmt(Stmt& stmt) {
  // Resolve expressions and declarations according to statement kind.
  switch (stmt.kind) {
    case StmtKind::VarDecl: {
      auto& node = static_cast<VarDecl&>(stmt);
      // Resolve the initializer before adding the new local to the scope.
      if (node.init) resolveExpr(*node.init);
      if (node.initIsRead)
        node.readSymbol = lookupStream(node.readStream, node.range, false);
      node.symbol = declare(SymbolKind::Local, node.name, node.type, node.range);
      return;
    }
    case StmtKind::ArrayDecl: {
      auto& node = static_cast<ArrayDecl&>(stmt);
      if (node.size) resolveExpr(*node.size);
      for (auto& value : node.init) resolveExpr(*value);
      node.symbol = declare(SymbolKind::LocalArray, node.name, node.elem, node.range);
      return;
    }
    case StmtKind::Assign: {
      auto& node = static_cast<Assign&>(stmt);
      // Check that the target exists and is writable.
      node.symbol = lookup(node.name, node.range);
      if (node.symbol) {
        if (node.symbol->kind == SymbolKind::ArrayParam)
          report(node.range, "cannot assign to array parameter '" + node.name + "'");
        if (node.symbol->kind == SymbolKind::InductionVar)
          report(node.range, "cannot assign to loop variable '" + node.name + "'");
        if (node.symbol->kind == SymbolKind::GlobalConst ||
            node.symbol->kind == SymbolKind::GlobalConstArray)
          report(node.range, "cannot assign to constant '" + node.name + "'");
        if (!node.index && (node.symbol->kind == SymbolKind::LocalArray ||
                            node.symbol->kind == SymbolKind::ArrayParam ||
                            node.symbol->kind == SymbolKind::GlobalConstArray))
          report(node.range, "cannot assign to array '" + node.name + "' without an index");
        if (node.index && node.symbol->kind != SymbolKind::LocalArray &&
            node.symbol->kind != SymbolKind::ArrayParam)
          report(node.range, "'" + node.name + "' is not an array");
      }
      if (node.index) resolveExpr(*node.index);
      if (node.value) resolveExpr(*node.value);
      if (node.valueIsRead)
        node.readSymbol = lookupStream(node.readStream, node.range, false);
      return;
    }
    case StmtKind::Write: {
      auto& node = static_cast<Write&>(stmt);
      node.symbol = lookupStream(node.stream, node.range, true);
      if (node.value) resolveExpr(*node.value);
      return;
    }
    case StmtKind::If: {
      auto& node = static_cast<If&>(stmt);
      // Conditions and both branches use their enclosing lexical scopes.
      resolveExpr(*node.cond);
      resolveBlock(*node.thenB);
      if (node.elseS) {
        if (node.elseS->kind == StmtKind::Block)
          resolveBlock(static_cast<Block&>(*node.elseS));
        else
          resolveStmt(*node.elseS);
      }
      return;
    }
    case StmtKind::For: {
      auto& node = static_cast<For&>(stmt);
      // The initializer uses the outer scope; the loop variable does not.
      if (node.init) resolveExpr(*node.init);
      scopes_.push();
      node.ivSymbol = declare(SymbolKind::InductionVar, node.iv, node.ivType,
                              node.range);
      if (node.limit) resolveExpr(*node.limit);
      if (node.step) resolveExpr(*node.step);
      if (node.body) resolveBlock(*node.body, false);
      scopes_.pop();
      return;
    }
    case StmtKind::Return: {
      auto& node = static_cast<Return&>(stmt);
      if (node.value) resolveExpr(*node.value);
      return;
    }
    case StmtKind::Block:
      // Standalone nested blocks introduce their own scope.
      resolveBlock(static_cast<Block&>(stmt));
      return;
  }
}

} // namespace minihls