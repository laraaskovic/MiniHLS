#include "sema/const_eval.hpp"
#include "sema/symbol.hpp"

#include <limits>

namespace minihls {

void ConstantEvaluator::report(Range range, const std::string& message) {
  diags_.error(range, message);
}

bool ConstantEvaluator::fitsType(Bits value, Type type) const {
  if (type.isSigned) {
    i128 signedValue = value.value();
    if (type.width == 128) return true;
    i128 min = -(static_cast<i128>(1) << (type.width - 1));
    i128 max = (static_cast<i128>(1) << (type.width - 1)) - 1;
    return signedValue >= min && signedValue <= max;
  }
  if (value.value() < 0) return false;
  return type.width == 128 || value.raw <= mask(type.width);
}

bool ConstantEvaluator::valueAsI128(Bits value, i128& result) const {
  result = value.value();
  return true;
}

std::optional<Bits> ConstantEvaluator::evalConstant(Symbol* symbol, bool diagnose) {
  if (!symbol || symbol->kind != SymbolKind::GlobalConst) return std::nullopt;
  auto cached = values_.find(symbol);
  if (cached != values_.end()) return cached->second;
  if (inProgress_.find(symbol) != inProgress_.end()) {
    if (diagnose) report(symbol->declRange, "cyclic constant definition");
    return std::nullopt;
  }
  auto found = constants_.find(symbol);
  if (found == constants_.end() || !found->second->init) return std::nullopt;
  inProgress_.insert(symbol);
  auto value = eval(*found->second->init, diagnose);
  inProgress_.erase(symbol);
  if (value) values_.emplace(symbol, *value);
  return value;
}

std::optional<Bits> ConstantEvaluator::eval(Expr& expr, bool diagnose) {
  Type type = expr.typeKnown ? expr.type : Type{128, false};
  switch (expr.kind) {
    case ExprKind::IntLit:
      return Bits::make(static_cast<IntLit&>(expr).value,
                        type.isPoly ? 128 : type.width,
                        type.isPoly ? false : type.isSigned);
    case ExprKind::NameRef: {
      auto& node = static_cast<NameRef&>(expr);
      return evalConstant(node.symbol, diagnose);
    }
    case ExprKind::Index:
      return std::nullopt;
    case ExprKind::Cast: {
      auto& node = static_cast<Cast&>(expr);
      auto operand = eval(*node.operand, diagnose);
      return operand ? std::optional<Bits>(castTo(*operand, node.type.width, node.type.isSigned))
                     : std::nullopt;
    }
    case ExprKind::Unary: {
      auto& node = static_cast<Unary&>(expr);
      auto operand = eval(*node.operand, diagnose);
      if (!operand) return std::nullopt;
      switch (node.op) {
        case Tok::Plus: return *operand;
        case Tok::Minus: return neg(*operand);
        case Tok::Tilde: return bitNot(*operand);
        case Tok::Bang: return Bits::make(operand->raw == 0, 1, false);
        default: return std::nullopt;
      }
    }
    case ExprKind::Binary: {
      auto& node = static_cast<Binary&>(expr);
      auto left = eval(*node.lhs, diagnose);
      auto right = eval(*node.rhs, diagnose);
      if (!left || !right) return std::nullopt;
      switch (node.op) {
        case Tok::Plus: return add(*left, *right);
        case Tok::Minus: return sub(*left, *right);
        case Tok::Star: return mul(*left, *right);
        case Tok::Slash: return divide(*left, *right);
        case Tok::Percent: return remainder(*left, *right);
        case Tok::Shl: return shl(*left, *right);
        case Tok::Shr: return shr(*left, *right);
        case Tok::Amp: return bitAnd(*left, *right);
        case Tok::Pipe: return bitOr(*left, *right);
        case Tok::Caret: return bitXor(*left, *right);
        case Tok::Lt: return compare(*left, *right, Cmp::Lt);
        case Tok::Le: return compare(*left, *right, Cmp::Le);
        case Tok::Gt: return compare(*left, *right, Cmp::Gt);
        case Tok::Ge: return compare(*left, *right, Cmp::Ge);
        case Tok::EqEq: return compare(*left, *right, Cmp::Eq);
        case Tok::BangEq: return compare(*left, *right, Cmp::Ne);
        case Tok::AmpAmp: return Bits::make(left->raw != 0 && right->raw != 0, 1, false);
        case Tok::PipePipe: return Bits::make(left->raw != 0 || right->raw != 0, 1, false);
        default: return std::nullopt;
      }
    }
    case ExprKind::Ternary: {
      auto& node = static_cast<Ternary&>(expr);
      auto condition = eval(*node.cond, diagnose);
      if (!condition) return std::nullopt;
      return condition->raw ? eval(*node.thenE, diagnose) : eval(*node.elseE, diagnose);
    }
  }
  return std::nullopt;
}

void ConstantEvaluator::validateArray(Expr* size, Symbol* symbol, Range range) {
  if (!size) return;
  auto value = eval(*size);
  if (!value) {
    report(range, "array size must be a constant");
    return;
  }
  if (value->value() < 1) {
    report(range, "array size must be at least 1");
    return;
  }
  if (value->raw > std::numeric_limits<uint64_t>::max()) {
    report(range, "array size is too large");
    return;
  }
  if (symbol) symbol->arrayLength = static_cast<uint64_t>(value->raw);
}

void ConstantEvaluator::validateIndex(Expr& expr) {
  if (expr.kind == ExprKind::Index) {
    auto& node = static_cast<Index&>(expr);
    if (node.index && node.symbol && node.symbol->arrayLength != 0) {
      auto index = eval(*node.index, false);
      if (index && index->value() >= 0 &&
          index->raw >= node.symbol->arrayLength)
        report(node.index->range, "constant array index is out of bounds");
    }
    if (node.index) validateIndex(*node.index);
    return;
  }
  if (expr.kind == ExprKind::Unary)
    validateIndex(*static_cast<Unary&>(expr).operand);
  else if (expr.kind == ExprKind::Cast)
    validateIndex(*static_cast<Cast&>(expr).operand);
  else if (expr.kind == ExprKind::Binary) {
    auto& node = static_cast<Binary&>(expr);
    validateIndex(*node.lhs); validateIndex(*node.rhs);
  } else if (expr.kind == ExprKind::Ternary) {
    auto& node = static_cast<Ternary&>(expr);
    validateIndex(*node.cond); validateIndex(*node.thenE); validateIndex(*node.elseE);
  }
}

void ConstantEvaluator::evaluateConst(ConstDecl& decl) {
  validateArray(decl.size.get(), decl.symbol, decl.range);
  if (decl.init) {
    auto value = eval(*decl.init);
    if (value) { decl.value = *value; decl.valueKnown = true; }
  }
  for (auto& value : decl.arrayInit) validateIndex(*value);
}

void ConstantEvaluator::evaluateFor(For& loop) {
  auto initial = loop.init ? eval(*loop.init) : std::nullopt;
  auto limit = loop.limit ? eval(*loop.limit) : std::nullopt;
  auto step = loop.step ? eval(*loop.step) : std::nullopt;
  if (!initial || !limit || !step) {
    report(loop.range, "for loop bounds must be constant");
    return;
  }
  i128 start, end, amount;
  valueAsI128(*initial, start); valueAsI128(*limit, end); valueAsI128(*step, amount);
  if (amount < 0) amount = -amount;
  if (amount == 0) { report(loop.range, "for loop step must not be zero"); return; }

  i128 count = 0;
  bool increasing = loop.stepIsAdd;
  bool enters = increasing ? (loop.relOp == Tok::Lt ? start < end : start <= end)
                           : (loop.relOp == Tok::Gt ? start > end : start >= end);
  if (enters) {
    i128 distance = increasing ? end - start : start - end;
    if ((increasing && (loop.relOp == Tok::Gt || loop.relOp == Tok::Ge)) ||
        (!increasing && (loop.relOp == Tok::Lt || loop.relOp == Tok::Le))) {
      report(loop.range, "for loop step moves away from its condition");
      return;
    }
    count = loop.relOp == Tok::Lt || loop.relOp == Tok::Gt
              ? (distance + amount - 1) / amount
              : distance / amount + 1;
  }
  i128 terminal = increasing ? start + count * amount : start - count * amount;
  if (!loop.ivType.isSigned && terminal < 0) {
    report(loop.range, "for loop terminating value does not fit its induction variable");
    return;
  }
  if (loop.ivType.isSigned && loop.ivType.width < 128) {
    i128 min = -(static_cast<i128>(1) << (loop.ivType.width - 1));
    i128 max = (static_cast<i128>(1) << (loop.ivType.width - 1)) - 1;
    if (terminal < min || terminal > max) {
      report(loop.range, "for loop terminating value does not fit its induction variable");
      return;
    }
  }
  Bits terminalBits = Bits::make(static_cast<u128>(terminal), loop.ivType.width,
                                 loop.ivType.isSigned);
  if (!fitsType(terminalBits, loop.ivType)) {
    report(loop.range, "for loop terminating value does not fit its induction variable");
    return;
  }
  if (count < 0 || static_cast<u128>(count) > std::numeric_limits<uint64_t>::max()) {
    report(loop.range, "for loop trip count is too large");
    return;
  }
  loop.tripCount = static_cast<uint64_t>(count);
  loop.tripCountKnown = true;
}

void ConstantEvaluator::evaluateStmt(Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::VarDecl: {
      auto& node = static_cast<VarDecl&>(stmt);
      if (node.init) validateIndex(*node.init);
      return;
    }
    case StmtKind::ArrayDecl: {
      auto& node = static_cast<ArrayDecl&>(stmt);
      validateArray(node.size.get(), node.symbol, node.range);
      for (auto& value : node.init) validateIndex(*value);
      return;
    }
    case StmtKind::Assign: {
      auto& node = static_cast<Assign&>(stmt);
      if (node.index) validateIndex(*node.index);
      if (node.value) validateIndex(*node.value);
      return;
    }
    case StmtKind::Write:
      if (static_cast<Write&>(stmt).value) validateIndex(*static_cast<Write&>(stmt).value);
      return;
    case StmtKind::If: {
      auto& node = static_cast<If&>(stmt);
      validateIndex(*node.cond); evaluateBlock(*node.thenB);
      if (node.elseS) evaluateStmt(*node.elseS);
      return;
    }
    case StmtKind::For: {
      auto& node = static_cast<For&>(stmt);
      evaluateFor(node); evaluateBlock(*node.body); return;
    }
    case StmtKind::Return:
      if (static_cast<Return&>(stmt).value) validateIndex(*static_cast<Return&>(stmt).value);
      return;
    case StmtKind::Block: evaluateBlock(static_cast<Block&>(stmt)); return;
  }
}

void ConstantEvaluator::evaluateBlock(Block& block) {
  for (auto& statement : block.stmts) evaluateStmt(*statement);
}

void ConstantEvaluator::evaluate(Program& program) {
  constants_.clear(); values_.clear(); inProgress_.clear();
  for (auto& decl : program.consts)
    if (decl.symbol) constants_[decl.symbol] = &decl;
  for (auto& decl : program.consts) evaluateConst(decl);
  for (auto& param : program.fn.params)
    if (param.kind == ParamKind::Array)
      validateArray(param.size.get(), param.symbol, param.range);
  if (program.fn.body) evaluateBlock(*program.fn.body);
}

} // namespace minihls