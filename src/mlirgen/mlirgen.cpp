#include "mlirgen/mlirgen.hpp"

#include "support/bits.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Verifier.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace minihls::mlirgen {
namespace {

using ast::BinaryOp;
using ast::Expr;
using ast::ExprKind;
using ast::Stmt;
using ast::StmtKind;
using sema::Symbol;
using sema::SymbolKind;

// True if `stmt` contains a return anywhere inside it.
bool contains_return(const Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::Return:
      return true;
    case StmtKind::Block:
      return std::any_of(stmt.statements.begin(), stmt.statements.end(),
                         [](const auto& child) { return contains_return(*child); });
    case StmtKind::If:
      return contains_return(*stmt.then_branch) ||
             (stmt.else_branch && contains_return(*stmt.else_branch));
    default:
      // Sema rejects returns inside loops.
      return false;
  }
}

// Every scalar symbol assigned anywhere inside `stmt`.
void collect_assigned(const Stmt& stmt, std::set<int>& out) {
  switch (stmt.kind) {
    case StmtKind::Block:
      for (const auto& child : stmt.statements) collect_assigned(*child, out);
      return;
    case StmtKind::Assign:
      if (stmt.target->kind == ExprKind::Name) out.insert(stmt.target->symbol);
      return;
    case StmtKind::If:
      collect_assigned(*stmt.then_branch, out);
      if (stmt.else_branch) collect_assigned(*stmt.else_branch, out);
      return;
    case StmtKind::For:
      collect_assigned(*stmt.for_init, out);
      collect_assigned(*stmt.for_step, out);
      collect_assigned(*stmt.body, out);
      return;
    default:
      return;
  }
}

class Generator {
 public:
  Generator(mlir::MLIRContext& context, const sema::FunctionInfo& function,
            const SourceFile& file, DiagnosticEngine& diagnostics)
      : context_(context),
        function_(function),
        file_(file),
        diagnostics_(diagnostics),
        builder_(&context) {}

  mlir::OwningOpRef<mlir::ModuleOp> run();

 private:
  // Helpers ---------------------------------------------------------------

  mlir::Location loc(SourceRange range) const;
  mlir::IntegerType int_type(unsigned width) const {
    return builder_.getIntegerType(width);
  }
  const Symbol& symbol(int index) const {
    return function_.symbols[static_cast<std::size_t>(index)];
  }
  mlir::Value constant(unsigned width, std::uint64_t bits, mlir::Location at);
  mlir::Value index_constant(std::int64_t value, mlir::Location at);
  // Brings `value`, read with the given signedness, to `width` bits.
  mlir::Value extend(mlir::Value value, bool is_signed, unsigned width, mlir::Location at);
  // A one-bit "is nonzero" for conditions.
  mlir::Value truth(mlir::Value value, mlir::Location at);
  unsigned width_of(mlir::Value value) const {
    return value.getType().getIntOrFloatBitWidth();
  }
  mlir::Value index_value(const Expr& index);

  // Expressions -----------------------------------------------------------

  mlir::Value gen_expr(const Expr& expr);
  mlir::Value gen_binary(const Expr& expr);
  mlir::Value gen_divide(mlir::Value a, mlir::Value b, bool is_signed, bool remainder,
                         mlir::Location at);
  mlir::Value gen_shift(BinaryOp op, bool is_signed, mlir::Value value, mlir::Value amount,
                        mlir::Location at);

  // Statements ------------------------------------------------------------

  // A statement that is not in tail position, and so contains no return.
  void gen_stmt(const Stmt& stmt);
  void gen_if(const Stmt& stmt);
  void gen_for(const Stmt& stmt);
  // Lowers a statement list that ends the function, returning the value it
  // returns (null for a void function). An `if` containing a return is lowered
  // with the rest of the list copied into both branches, which keeps every
  // return in tail position: scf.if cannot jump out of the function.
  mlir::Value gen_tail(std::vector<const Stmt*> statements);
  // Scalar symbols assigned inside `stmt` that already exist outside it: the
  // values an scf.if or scf.for has to carry out.
  std::vector<int> carried_by(const Stmt& stmt) const;

  void allocate_arrays(mlir::Location at);
  bool unsupported(SourceRange range, const std::string& what);

  mlir::MLIRContext& context_;
  const sema::FunctionInfo& function_;
  const SourceFile& file_;
  DiagnosticEngine& diagnostics_;
  mlir::OpBuilder builder_;
  mlir::ModuleOp module_;

  // Current SSA value of each scalar symbol; null when not in scope.
  std::vector<mlir::Value> values_;
  // The memref holding each array symbol.
  std::vector<mlir::Value> memrefs_;
  bool failed_ = false;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

mlir::Location Generator::loc(SourceRange range) const {
  const LineColumn position = file_.resolve(range.begin);
  return mlir::FileLineColLoc::get(&context_, file_.path(), position.line, position.column);
}

mlir::Value Generator::constant(unsigned width, std::uint64_t bits, mlir::Location at) {
  const mlir::IntegerType type = int_type(width);
  return builder_.create<mlir::arith::ConstantOp>(
      at, builder_.getIntegerAttr(type, llvm::APInt(width, truncate(bits, width))));
}

mlir::Value Generator::index_constant(std::int64_t value, mlir::Location at) {
  return builder_.create<mlir::arith::ConstantIndexOp>(at, value);
}

mlir::Value Generator::extend(mlir::Value value, bool is_signed, unsigned width,
                              mlir::Location at) {
  const unsigned from = width_of(value);
  if (from == width) return value;
  if (from > width) return builder_.create<mlir::arith::TruncIOp>(at, int_type(width), value);
  if (is_signed) return builder_.create<mlir::arith::ExtSIOp>(at, int_type(width), value);
  return builder_.create<mlir::arith::ExtUIOp>(at, int_type(width), value);
}

mlir::Value Generator::truth(mlir::Value value, mlir::Location at) {
  if (width_of(value) == 1) return value;
  return builder_.create<mlir::arith::CmpIOp>(at, mlir::arith::CmpIPredicate::ne, value,
                                              constant(width_of(value), 0, at));
}

mlir::Value Generator::index_value(const Expr& index) {
  const mlir::Value value = gen_expr(index);
  const mlir::Location at = loc(index.range);
  // memref subscripts have the `index` type. A signed index is sign-extended,
  // so a negative one stays negative (and out of bounds) rather than wrapping.
  if (index.is_signed) {
    return builder_.create<mlir::arith::IndexCastOp>(at, builder_.getIndexType(), value);
  }
  return builder_.create<mlir::arith::IndexCastUIOp>(at, builder_.getIndexType(), value);
}

bool Generator::unsupported(SourceRange range, const std::string& what) {
  diagnostics_.error(range, what);
  failed_ = true;
  return false;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

mlir::Value Generator::gen_expr(const Expr& expr) {
  const mlir::Location at = loc(expr.range);
  // Sema has folded every constant expression already.
  if (expr.is_constant) return constant(expr.width, expr.constant, at);

  switch (expr.kind) {
    case ExprKind::IntLiteral:
      return constant(expr.width, expr.value, at);

    case ExprKind::Name:
      return values_[static_cast<std::size_t>(expr.symbol)];

    case ExprKind::Index: {
      const mlir::Value index = index_value(*expr.rhs);
      return builder_.create<mlir::memref::LoadOp>(
          at, memrefs_[static_cast<std::size_t>(expr.lhs->symbol)], mlir::ValueRange{index});
    }

    case ExprKind::Call:
      unsupported(expr.range, "stream reads are supported from milestone P5");
      return constant(expr.width, 0, at);

    case ExprKind::Unary: {
      const mlir::Value operand = gen_expr(*expr.lhs);
      switch (expr.unary_op) {
        case ast::UnaryOp::Negate:
          return builder_.create<mlir::arith::SubIOp>(
              at, constant(expr.width, 0, at),
              extend(operand, expr.lhs->is_signed, expr.width, at));
        case ast::UnaryOp::BitNot:
          return builder_.create<mlir::arith::XOrIOp>(
              at, operand, constant(expr.width, mask_for_width(expr.width), at));
        case ast::UnaryOp::LogicalNot:
          return builder_.create<mlir::arith::CmpIOp>(at, mlir::arith::CmpIPredicate::eq,
                                                      operand,
                                                      constant(width_of(operand), 0, at));
      }
      return operand;
    }

    case ExprKind::Binary:
      return gen_binary(expr);

    case ExprKind::Conditional: {
      const mlir::Value condition = truth(gen_expr(*expr.lhs), at);
      const mlir::Value if_true =
          extend(gen_expr(*expr.rhs), expr.rhs->is_signed, expr.width, at);
      const mlir::Value if_false =
          extend(gen_expr(*expr.third), expr.third->is_signed, expr.width, at);
      return builder_.create<mlir::arith::SelectOp>(at, condition, if_true, if_false);
    }
  }
  return constant(expr.width, 0, at);
}

mlir::Value Generator::gen_binary(const Expr& expr) {
  const mlir::Location at = loc(expr.range);
  // Operands in source order.
  const mlir::Value lhs = gen_expr(*expr.lhs);
  const mlir::Value rhs = gen_expr(*expr.rhs);
  const bool lhs_signed = expr.lhs->is_signed;
  const bool rhs_signed = expr.rhs->is_signed;
  const unsigned width = expr.width;

  // Comparisons and division need both operands at one common width: the
  // mixed-signedness rule (an unsigned operand beside a signed one gains a
  // bit), then the wider of the two.
  const bool any_signed = lhs_signed || rhs_signed;
  const bool mixed = lhs_signed != rhs_signed;
  const unsigned common = std::max(width_of(lhs) + (mixed && !lhs_signed ? 1u : 0u),
                                   width_of(rhs) + (mixed && !rhs_signed ? 1u : 0u));
  auto at_width = [&](unsigned w) {
    return std::pair{extend(lhs, lhs_signed, w, at), extend(rhs, rhs_signed, w, at)};
  };

  using mlir::arith::CmpIPredicate;
  switch (expr.binary_op) {
    case BinaryOp::Add: {
      const auto [a, b] = at_width(width);
      return builder_.create<mlir::arith::AddIOp>(at, a, b);
    }
    case BinaryOp::Subtract: {
      const auto [a, b] = at_width(width);
      return builder_.create<mlir::arith::SubIOp>(at, a, b);
    }
    case BinaryOp::Multiply: {
      const auto [a, b] = at_width(width);
      return builder_.create<mlir::arith::MulIOp>(at, a, b);
    }
    case BinaryOp::BitAnd: {
      const auto [a, b] = at_width(width);
      return builder_.create<mlir::arith::AndIOp>(at, a, b);
    }
    case BinaryOp::BitOr: {
      const auto [a, b] = at_width(width);
      return builder_.create<mlir::arith::OrIOp>(at, a, b);
    }
    case BinaryOp::BitXor: {
      const auto [a, b] = at_width(width);
      return builder_.create<mlir::arith::XOrIOp>(at, a, b);
    }

    case BinaryOp::Divide:
    case BinaryOp::Modulo: {
      const auto [a, b] = at_width(common);
      const mlir::Value result =
          gen_divide(a, b, any_signed, expr.binary_op == BinaryOp::Modulo, at);
      return extend(result, any_signed, width, at);
    }

    case BinaryOp::ShiftLeft:
      return gen_shift(expr.binary_op, lhs_signed, extend(lhs, lhs_signed, width, at), rhs, at);
    case BinaryOp::ShiftRight:
      return gen_shift(expr.binary_op, lhs_signed, lhs, rhs, at);

    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual: {
      const auto [a, b] = at_width(common);
      CmpIPredicate predicate = CmpIPredicate::eq;
      switch (expr.binary_op) {
        case BinaryOp::Equal: predicate = CmpIPredicate::eq; break;
        case BinaryOp::NotEqual: predicate = CmpIPredicate::ne; break;
        case BinaryOp::Less: predicate = any_signed ? CmpIPredicate::slt : CmpIPredicate::ult; break;
        case BinaryOp::LessEqual: predicate = any_signed ? CmpIPredicate::sle : CmpIPredicate::ule; break;
        case BinaryOp::Greater: predicate = any_signed ? CmpIPredicate::sgt : CmpIPredicate::ugt; break;
        default: predicate = any_signed ? CmpIPredicate::sge : CmpIPredicate::uge; break;
      }
      return builder_.create<mlir::arith::CmpIOp>(at, predicate, a, b);
    }

    case BinaryOp::LogicalAnd:
      return builder_.create<mlir::arith::AndIOp>(at, truth(lhs, at), truth(rhs, at));
    case BinaryOp::LogicalOr:
      return builder_.create<mlir::arith::OrIOp>(at, truth(lhs, at), truth(rhs, at));
  }
  return constant(width, 0, at);
}

mlir::Value Generator::gen_divide(mlir::Value a, mlir::Value b, bool is_signed, bool remainder,
                                  mlir::Location at) {
  using mlir::arith::CmpIPredicate;
  const unsigned width = width_of(a);
  const mlir::Value zero = constant(width, 0, at);
  const mlir::Value one = constant(width, 1, at);
  const mlir::Value by_zero = builder_.create<mlir::arith::CmpIOp>(at, CmpIPredicate::eq, b, zero);

  // arith leaves division by zero -- and, for signed division, the one
  // quotient that overflows, min / -1 -- undefined. Divide by 1 instead in
  // those cases, then select the result the language defines.
  mlir::Value unsafe = by_zero;
  mlir::Value overflow;
  if (is_signed) {
    const std::uint64_t min_bits = std::uint64_t{1} << (width - 1);
    const mlir::Value is_min = builder_.create<mlir::arith::CmpIOp>(
        at, CmpIPredicate::eq, a, constant(width, min_bits, at));
    const mlir::Value is_minus_one = builder_.create<mlir::arith::CmpIOp>(
        at, CmpIPredicate::eq, b, constant(width, mask_for_width(width), at));
    overflow = builder_.create<mlir::arith::AndIOp>(at, is_min, is_minus_one);
    unsafe = builder_.create<mlir::arith::OrIOp>(at, by_zero, overflow);
  }
  const mlir::Value divisor = builder_.create<mlir::arith::SelectOp>(at, unsafe, one, b);

  mlir::Value result;
  if (remainder) {
    result = is_signed ? mlir::Value(builder_.create<mlir::arith::RemSIOp>(at, a, divisor))
                       : mlir::Value(builder_.create<mlir::arith::RemUIOp>(at, a, divisor));
    // x % 0 is x; min % -1 is 0, which dividing by 1 already gives.
    return builder_.create<mlir::arith::SelectOp>(at, by_zero, a, result);
  }
  result = is_signed ? mlir::Value(builder_.create<mlir::arith::DivSIOp>(at, a, divisor))
                     : mlir::Value(builder_.create<mlir::arith::DivUIOp>(at, a, divisor));
  // x / 0 is all ones; min / -1 wraps to min, which dividing by 1 gives.
  return builder_.create<mlir::arith::SelectOp>(at, by_zero,
                                                constant(width, mask_for_width(width), at),
                                                result);
}

mlir::Value Generator::gen_shift(BinaryOp op, bool is_signed, mlir::Value value,
                                 mlir::Value amount, mlir::Location at) {
  using mlir::arith::CmpIPredicate;
  const unsigned width = width_of(value);
  const unsigned amount_width = width_of(amount);

  // arith requires the amount to have the value's type, and a shift by the
  // width or more is undefined there. The language reads the amount as
  // unsigned and defines those shifts, so compare first, then shift by an
  // amount known to be in range.
  const bool can_overflow =
      amount_width >= 64 || (std::uint64_t{1} << amount_width) - 1 >= width;
  mlir::Value too_far;
  if (can_overflow) {
    // `width` fits in the amount's own width whenever overflow is possible.
    too_far = builder_.create<mlir::arith::CmpIOp>(at, CmpIPredicate::uge, amount,
                                                   constant(amount_width, width, at));
  }
  // In range, the amount is below `width`, so it fits in `width` bits.
  mlir::Value in_range = extend(amount, false, width, at);

  if (op == BinaryOp::ShiftRight && is_signed) {
    // Shifting a signed value right by width - 1 already gives all sign bits.
    if (too_far) {
      in_range = builder_.create<mlir::arith::SelectOp>(at, too_far,
                                                        constant(width, width - 1, at),
                                                        in_range);
    }
    return builder_.create<mlir::arith::ShRSIOp>(at, value, in_range);
  }

  if (too_far) {
    in_range =
        builder_.create<mlir::arith::SelectOp>(at, too_far, constant(width, 0, at), in_range);
  }
  const mlir::Value shifted =
      op == BinaryOp::ShiftLeft
          ? mlir::Value(builder_.create<mlir::arith::ShLIOp>(at, value, in_range))
          : mlir::Value(builder_.create<mlir::arith::ShRUIOp>(at, value, in_range));
  if (!too_far) return shifted;
  return builder_.create<mlir::arith::SelectOp>(at, too_far, constant(width, 0, at), shifted);
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

std::vector<int> Generator::carried_by(const Stmt& stmt) const {
  std::set<int> assigned;
  collect_assigned(stmt, assigned);
  std::vector<int> carried;
  for (const int symbol : assigned) {
    if (values_[static_cast<std::size_t>(symbol)]) carried.push_back(symbol);
  }
  return carried;
}

void Generator::gen_stmt(const Stmt& stmt) {
  const mlir::Location at = loc(stmt.range);
  switch (stmt.kind) {
    case StmtKind::Block:
      for (const auto& child : stmt.statements) gen_stmt(*child);
      return;

    case StmtKind::VarDecl: {
      const Symbol& s = symbol(stmt.symbol);
      if (s.kind != SymbolKind::Scalar) return;  // arrays are allocated on entry
      mlir::Value value = constant(s.type.width, 0, at);
      if (!stmt.init.empty()) {
        const Expr& init = *stmt.init.front();
        value = extend(gen_expr(init), init.is_signed, s.type.width, at);
      }
      values_[static_cast<std::size_t>(stmt.symbol)] = value;
      return;
    }

    case StmtKind::Assign: {
      const Expr& target = *stmt.target;
      if (target.kind == ExprKind::Name) {
        const Symbol& s = symbol(target.symbol);
        const mlir::Value value = gen_expr(*stmt.value);
        values_[static_cast<std::size_t>(target.symbol)] =
            extend(value, stmt.value->is_signed, s.type.width, at);
        return;
      }
      // The value first, then the index, as the interpreter evaluates them.
      const mlir::Value value = gen_expr(*stmt.value);
      const mlir::Value index = index_value(*target.rhs);
      const Symbol& s = symbol(target.lhs->symbol);
      builder_.create<mlir::memref::StoreOp>(
          at, extend(value, stmt.value->is_signed, s.type.width, at),
          memrefs_[static_cast<std::size_t>(target.lhs->symbol)], mlir::ValueRange{index});
      return;
    }

    case StmtKind::Call:
      unsupported(stmt.range, "stream writes are supported from milestone P5");
      return;

    case StmtKind::If:
      gen_if(stmt);
      return;

    case StmtKind::For:
      gen_for(stmt);
      return;

    case StmtKind::Return:
      // Only reached if a return is not in tail position, which gen_tail
      // prevents.
      unsupported(stmt.range, "internal error: return outside tail position");
      return;

    case StmtKind::Pragma:
      return;
  }
}

void Generator::gen_if(const Stmt& stmt) {
  const mlir::Location at = loc(stmt.range);
  const mlir::Value condition = truth(gen_expr(*stmt.value), at);
  const std::vector<int> carried = carried_by(stmt);
  const std::vector<mlir::Value> before = values_;

  // Each branch yields the final value of every carried variable; a branch
  // that does not assign one yields the value it came in with.
  auto branch = [&](const Stmt* body) {
    return [&, body](mlir::OpBuilder&, mlir::Location) {
      values_ = before;
      if (body) gen_stmt(*body);
      llvm::SmallVector<mlir::Value> yielded;
      for (const int s : carried) yielded.push_back(values_[static_cast<std::size_t>(s)]);
      builder_.create<mlir::scf::YieldOp>(at, yielded);
    };
  };
  auto if_op = builder_.create<mlir::scf::IfOp>(at, condition, branch(stmt.then_branch.get()),
                                                branch(stmt.else_branch.get()));
  values_ = before;
  for (std::size_t i = 0; i < carried.size(); ++i) {
    values_[static_cast<std::size_t>(carried[i])] = if_op.getResult(static_cast<unsigned>(i));
  }
}

void Generator::gen_for(const Stmt& stmt) {
  const mlir::Location at = loc(stmt.range);
  const sema::LoopInfo& loop = function_.loops.at(&stmt);

  gen_stmt(*stmt.for_init);
  if (loop.trip_count == 0) return;

  // Carried: the induction variable, then everything else the body assigns.
  std::vector<int> carried{loop.induction};
  for (const int s : carried_by(*stmt.body)) {
    if (s != loop.induction) carried.push_back(s);
  }
  llvm::SmallVector<mlir::Value> initial;
  for (const int s : carried) initial.push_back(values_[static_cast<std::size_t>(s)]);
  const std::vector<mlir::Value> before = values_;

  // The loop counts iterations; the induction variable is carried separately,
  // since sema has already proven how many iterations its header allows.
  auto for_op = builder_.create<mlir::scf::ForOp>(
      at, index_constant(0, at), index_constant(static_cast<std::int64_t>(loop.trip_count), at),
      index_constant(1, at), initial,
      [&](mlir::OpBuilder&, mlir::Location, mlir::Value, mlir::ValueRange arguments) {
        for (std::size_t i = 0; i < carried.size(); ++i) {
          values_[static_cast<std::size_t>(carried[i])] = arguments[i];
        }
        gen_stmt(*stmt.body);
        gen_stmt(*stmt.for_step);
        llvm::SmallVector<mlir::Value> yielded;
        for (const int s : carried) yielded.push_back(values_[static_cast<std::size_t>(s)]);
        builder_.create<mlir::scf::YieldOp>(at, yielded);
      });

  // Pragmas travel as attributes for the passes that act on them.
  if (loop.pipeline_ii != 0) {
    for_op->setAttr("minihls.pipeline_ii",
                    builder_.getI64IntegerAttr(static_cast<std::int64_t>(loop.pipeline_ii)));
  }
  if (loop.unroll_factor != 0) {
    for_op->setAttr("minihls.unroll",
                    builder_.getI64IntegerAttr(static_cast<std::int64_t>(loop.unroll_factor)));
  }

  values_ = before;
  for (std::size_t i = 0; i < carried.size(); ++i) {
    values_[static_cast<std::size_t>(carried[i])] = for_op.getResult(static_cast<unsigned>(i));
  }
}

mlir::Value Generator::gen_tail(std::vector<const Stmt*> statements) {
  for (std::size_t i = 0; i < statements.size(); ++i) {
    const Stmt& stmt = *statements[i];
    const std::vector<const Stmt*> rest(statements.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                        statements.end());

    if (stmt.kind == StmtKind::Return) {
      if (!stmt.value) return {};
      return extend(gen_expr(*stmt.value), stmt.value->is_signed,
                    function_.return_type.width, loc(stmt.range));
    }

    if (stmt.kind == StmtKind::Block && contains_return(stmt)) {
      // Names are resolved to symbols already, so flattening a block into the
      // statements after it cannot change what any name refers to.
      std::vector<const Stmt*> flattened;
      for (const auto& child : stmt.statements) flattened.push_back(child.get());
      flattened.insert(flattened.end(), rest.begin(), rest.end());
      return gen_tail(std::move(flattened));
    }

    if (stmt.kind == StmtKind::If && contains_return(stmt)) {
      const mlir::Location at = loc(stmt.range);
      const mlir::Value condition = truth(gen_expr(*stmt.value), at);
      const std::vector<mlir::Value> before = values_;
      auto branch = [&](const Stmt* first) {
        return [&, first](mlir::OpBuilder&, mlir::Location) {
          values_ = before;
          std::vector<const Stmt*> path;
          if (first) path.push_back(first);
          path.insert(path.end(), rest.begin(), rest.end());
          const mlir::Value result = gen_tail(std::move(path));
          if (result) {
            builder_.create<mlir::scf::YieldOp>(at, result);
          } else {
            builder_.create<mlir::scf::YieldOp>(at);
          }
        };
      };
      auto if_op = builder_.create<mlir::scf::IfOp>(
          at, condition, branch(stmt.then_branch.get()), branch(stmt.else_branch.get()));
      values_ = before;
      return if_op.getNumResults() ? if_op.getResult(0) : mlir::Value();
    }

    gen_stmt(stmt);
  }
  // Falling off the end is only possible in a void function; sema proves every
  // path of a non-void function returns.
  if (!function_.returns_void) {
    return constant(function_.return_type.width, 0, builder_.getUnknownLoc());
  }
  return {};
}

void Generator::allocate_arrays(mlir::Location at) {
  for (std::size_t i = 0; i < function_.symbols.size(); ++i) {
    const Symbol& s = function_.symbols[i];
    if (s.kind != SymbolKind::Array || s.is_param) continue;

    // The initial contents live in a constant global. A const array is read
    // from it directly; any other local array is a stack buffer copied from it
    // on every call, since a local array starts from its initializer each time.
    const auto type = mlir::MemRefType::get({static_cast<std::int64_t>(s.array_size)},
                                            int_type(s.type.width));
    llvm::SmallVector<llvm::APInt> contents;
    for (const std::uint64_t bits : s.initial) contents.emplace_back(s.type.width, bits);
    const auto tensor = mlir::RankedTensorType::get(
        {static_cast<std::int64_t>(s.array_size)}, int_type(s.type.width));
    const std::string name = function_.function->name + "_" + s.name + "_init";
    {
      mlir::OpBuilder::InsertionGuard guard(builder_);
      builder_.setInsertionPointToStart(module_.getBody());
      builder_.create<mlir::memref::GlobalOp>(
          at, name, builder_.getStringAttr("private"), type,
          mlir::DenseElementsAttr::get(tensor, contents), /*constant=*/true,
          /*alignment=*/mlir::IntegerAttr());
    }
    const mlir::Value global = builder_.create<mlir::memref::GetGlobalOp>(at, type, name);
    if (s.is_const) {
      memrefs_[i] = global;
      continue;
    }
    const mlir::Value buffer = builder_.create<mlir::memref::AllocaOp>(at, type);
    builder_.create<mlir::scf::ForOp>(
        at, index_constant(0, at), index_constant(static_cast<std::int64_t>(s.array_size), at),
        index_constant(1, at), mlir::ValueRange{},
        [&](mlir::OpBuilder&, mlir::Location, mlir::Value k, mlir::ValueRange) {
          const mlir::Value element =
              builder_.create<mlir::memref::LoadOp>(at, global, mlir::ValueRange{k});
          builder_.create<mlir::memref::StoreOp>(at, element, buffer, mlir::ValueRange{k});
          builder_.create<mlir::scf::YieldOp>(at);
        });
    memrefs_[i] = buffer;
  }
}

mlir::OwningOpRef<mlir::ModuleOp> Generator::run() {
  const ast::Function& source = *function_.function;
  const mlir::Location at = loc(source.range);
  mlir::OwningOpRef<mlir::ModuleOp> module = mlir::ModuleOp::create(at);
  module_ = *module;
  values_.assign(function_.symbols.size(), mlir::Value());
  memrefs_.assign(function_.symbols.size(), mlir::Value());

  // The signature: scalars as integers, arrays as memrefs.
  llvm::SmallVector<mlir::Type> inputs;
  for (const int index : function_.params) {
    const Symbol& s = symbol(index);
    switch (s.kind) {
      case SymbolKind::Scalar:
        inputs.push_back(int_type(s.type.width));
        break;
      case SymbolKind::Array:
        inputs.push_back(mlir::MemRefType::get({static_cast<std::int64_t>(s.array_size)},
                                               int_type(s.type.width)));
        break;
      case SymbolKind::Stream:
        unsupported(s.range, "stream parameters are supported from milestone P5");
        return nullptr;
    }
  }
  llvm::SmallVector<mlir::Type> results;
  if (!function_.returns_void) results.push_back(int_type(function_.return_type.width));

  builder_.setInsertionPointToEnd(module_.getBody());
  auto func = builder_.create<mlir::func::FuncOp>(at, source.name,
                                                  builder_.getFunctionType(inputs, results));
  mlir::Block* entry = func.addEntryBlock();
  builder_.setInsertionPointToStart(entry);

  for (std::size_t i = 0; i < function_.params.size(); ++i) {
    const auto index = static_cast<std::size_t>(function_.params[i]);
    const mlir::Value argument = entry->getArgument(static_cast<unsigned>(i));
    if (function_.symbols[index].kind == SymbolKind::Scalar) {
      values_[index] = argument;
    } else {
      memrefs_[index] = argument;
    }
  }
  allocate_arrays(at);

  std::vector<const Stmt*> body;
  for (const auto& stmt : source.body->statements) body.push_back(stmt.get());
  const mlir::Value result = gen_tail(std::move(body));
  if (failed_) return nullptr;
  if (result) {
    builder_.create<mlir::func::ReturnOp>(at, result);
  } else {
    builder_.create<mlir::func::ReturnOp>(at);
  }

  if (mlir::failed(mlir::verify(*module))) {
    diagnostics_.error(source.range, "internal error: MLIRGen produced invalid IR");
    return nullptr;
  }
  return module;
}

}  // namespace

void load_dialects(mlir::MLIRContext& context) {
  context.loadDialect<mlir::arith::ArithDialect, mlir::func::FuncDialect,
                      mlir::memref::MemRefDialect, mlir::scf::SCFDialect>();
}

mlir::OwningOpRef<mlir::ModuleOp> generate(mlir::MLIRContext& context,
                                           const sema::FunctionInfo& function,
                                           const SourceFile& file,
                                           DiagnosticEngine& diagnostics) {
  load_dialects(context);
  return Generator(context, function, file, diagnostics).run();
}

}  // namespace minihls::mlirgen
