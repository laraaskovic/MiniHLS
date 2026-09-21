#include "mlirgen/mlirgen.hpp"
#include "sema/symbol.hpp"             // ast.hpp only forward-declares Symbol
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <iostream>

namespace minihls {
namespace {

// NOTE on `Op::create(builder, loc, ...)`: the older `builder.create<Op>(loc, ...)`
// spelling still compiles, but LLVM marked it [[deprecated]] — every use would
// warn under -Wall. Same operation, arguments shifted by one.
class MLIRGen {
public:
  MLIRGen(mlir::MLIRContext& ctx, const SourceFile& src, Diagnostics& diags)
      : builder_(&ctx), src_(src), diags_(diags) {}

  mlir::OwningOpRef<mlir::ModuleOp> emit(Program& program) {
    // OwningOpRef from the moment it exists. In MLIR an operation is owned by
    // its parent block, and a top-level module has no parent — so this is the
    // only thing that will ever free it. Writing `auto` here hands back a raw
    // ModuleOp, and every `return {}` below then leaks the whole module.
    mlir::OwningOpRef<mlir::ModuleOp> module =
        mlir::ModuleOp::create(builder_.getUnknownLoc());
    builder_.setInsertionPointToEnd(module->getBody());

    // File-scope constants are emitted on demand at each read; see NameRef.
    // Const arrays become memories in S7.
    for (ConstDecl& c : program.consts)
      if (!c.isArray && c.valueKnown) constants_[c.symbol] = &c;

    emitFunction(program.fn);
    if (failed_) return {};
    return module;
  }

private:
  mlir::Location loc(Range r) {
    return mlir::FileLineColLoc::get(builder_.getStringAttr(src_.path()),
                                     src_.line(r.begin), src_.column(r.begin));
  }

  // Our Type -> an MLIR type. Signedness is NOT carried: MLIR integers are
  // signless, exactly like a Verilog wire. It comes back in the choice of
  // operation — extsi vs extui, divsi vs divui, cmpi slt vs ult.
  //
  // A poly type is an untyped literal that sema never pinned down. Its width
  // is the default 1, so emitting it would silently produce an i1 of the
  // right value and the wrong width — abort instead of guessing.
  mlir::Type typeOf(Type t) {
    assert(!t.isPoly && "MLIRGen reached an expression sema left untyped");
    return builder_.getIntegerType(t.width);
  }

  // Every refusal goes through here, so an unsupported construct produces a
  // diagnostic and no IR — never half a module that fails the verifier.
  mlir::Value fail(Range where, const std::string& message) {
    diags_.error(where, message);
    failed_ = true;
    return {};
  }

  // ---- width plumbing (E4.S3) ----

  // Extend or truncate `v` to `toWidth`. Which extension to use is the
  // language's rule stated once: an operand narrower than the operation is
  // sign-extended when signed and zero-extended when unsigned, and a
  // narrowing keeps the low bits. `fromSigned` is the SOURCE's signedness,
  // never the destination's — that distinction is the whole of `u16(m)`.
  mlir::Value coerce(mlir::Location at, mlir::Value v, bool fromSigned,
                     unsigned toWidth) {
    const unsigned from = v.getType().getIntOrFloatBitWidth();
    if (from == toWidth) return v;
    mlir::Type to = builder_.getIntegerType(toWidth);
    if (from > toWidth) return mlir::arith::TruncIOp::create(builder_, at, to, v);
    if (fromSigned)     return mlir::arith::ExtSIOp::create(builder_, at, to, v);
    return mlir::arith::ExtUIOp::create(builder_, at, to, v);
  }

  // A literal of any width up to 128 bits. APInt takes the words low first,
  // and ignores the ones a narrow type does not need.
  mlir::Value intConst(mlir::Location at, u128 raw, unsigned width) {
    llvm::APInt bits(width, {static_cast<uint64_t>(raw),
                             static_cast<uint64_t>(raw >> 64)});
    auto ty = builder_.getIntegerType(width);
    return mlir::arith::ConstantOp::create(builder_, at, ty,
                                           builder_.getIntegerAttr(ty, bits));
  }

  mlir::Value constant(mlir::Location at, u128 raw, Type t) {
    assert(!t.isPoly && "MLIRGen reached a literal sema left untyped");
    return intConst(at, raw, t.width);
  }

  // How many bits it takes to hold `v` as an unsigned number.
  static unsigned bitsFor(u128 v) {
    unsigned n = 1;
    while (v >>= 1) ++n;
    return n;
  }

  void emitFunction(Function& fn) {
    llvm::SmallVector<mlir::Type> argTypes;
    for (Param& p : fn.params) {
      if (p.kind != ParamKind::Scalar) {
        fail(p.range, "parameter '" + p.name +
                      "': only scalar parameters are emitted yet "
                      "(arrays arrive in E4.S7, streams in E6)");
        return;
      }
      argTypes.push_back(typeOf(p.type));
    }

    returnType_ = fn.returnType;
    auto type = builder_.getFunctionType(argTypes, typeOf(fn.returnType));
    auto func = mlir::func::FuncOp::create(builder_, loc(fn.range), fn.name, type);
    auto* entry = func.addEntryBlock();
    builder_.setInsertionPointToStart(entry);

    // THIS is where an AST name becomes an SSA value for the first time.
    // After it, nothing downstream looks up "a" again — it looks up a Symbol*.
    for (size_t i = 0; i < fn.params.size(); ++i)
      values_[fn.params[i].symbol] = entry->getArgument(i);

    emitBlock(*fn.body);
    if (!failed_ && !returned_)
      fail(fn.range, "function '" + fn.name + "' falls off its end without a "
                     "return this stage can see");
  }

  // ---- expressions: returns the Value holding the result ----

  mlir::Value emit(Expr& e) {
    const auto at = loc(e.range);
    switch (e.kind) {

    case ExprKind::IntLit:
      // Sema already decided this literal's type. Build a constant of it.
      return constant(at, static_cast<IntLit&>(e).value, e.type);

    case ExprKind::NameRef: {
      // Reading a variable is a map lookup. No load, no memory — the value
      // is just whatever SSA value that symbol currently names.
      auto& n = static_cast<NameRef&>(e);
      if (mlir::Value v = values_.lookup(n.symbol)) return v;

      // ...unless it is a file-scope const, which has no SSA value at all
      // until a read asks for one. Emit a FRESH constant at every read and
      // do not cache it: the insertion point may be inside an scf.if or
      // scf.for region, and a cached value defined in that region would not
      // dominate a later use outside it. Duplicates are exactly what `cse`
      // exists to remove, so this costs nothing after E5.
      if (auto it = constants_.find(n.symbol); it != constants_.end())
        return constant(at, it->second->value.raw, it->second->type);

      return fail(e.range, "'" + n.name + "' has no value at this point");
    }

    case ExprKind::Cast: {
      auto& n = static_cast<Cast&>(e);
      mlir::Value v = emit(*n.operand);
      if (!v) return {};
      // T(e) widens by the SOURCE's signedness and narrows by keeping the
      // low bits. That is castTo() in bits.cpp, in one operation.
      return coerce(at, v, n.operand->type.isSigned, n.target.width);
    }

    case ExprKind::Unary: {
      auto& n = static_cast<Unary&>(e);
      mlir::Value v = emit(*n.operand);
      if (!v) return {};
      const Type ot = n.operand->type;
      switch (n.op) {

      case Tok::Plus:                      // +a is a, at the same width
        return v;

      case Tok::Minus: {
        // -a is i(wa+1). Widen FIRST, then subtract from zero, so the one
        // value that cannot be negated in wa bits has somewhere to go:
        // -i8(-128) is i9(128), not i8(-128) again.
        mlir::Value wide = coerce(at, v, ot.isSigned, e.type.width);
        return mlir::arith::SubIOp::create(
            builder_, at, intConst(at, 0, e.type.width), wide);
      }

      case Tok::Tilde:                     // ~a is a ^ all-ones, same width
        return mlir::arith::XOrIOp::create(
            builder_, at, v, intConst(at, mask(e.type.width), e.type.width));

      case Tok::Bang:                      // the operand is u1; !a is a == 0
        return mlir::arith::CmpIOp::create(
            builder_, at, mlir::arith::CmpIPredicate::eq, v,
            intConst(at, 0, ot.width));

      default:
        return fail(e.range, "unsupported unary operator");
      }
    }

    case ExprKind::Binary:
      return emitBinary(static_cast<Binary&>(e), at);

    case ExprKind::Ternary: {
      auto& n = static_cast<Ternary&>(e);
      // NOT a branch — arith.select is a mux, and BOTH arms are computed.
      // That matches LANGUAGE.md (nothing short-circuits) and matches the
      // comb.mux hand-written in E1.S5. `if` becomes scf.if in S5; this
      // does not.
      mlir::Value c = emit(*n.cond);
      mlir::Value t = emit(*n.thenE);
      mlir::Value f = emit(*n.elseE);
      if (!c || !t || !f) return {};
      // The arms meet at max(wa,wb), which is what the checker recorded.
      t = coerce(at, t, n.thenE->type.isSigned, e.type.width);
      f = coerce(at, f, n.elseE->type.isSigned, e.type.width);
      return mlir::arith::SelectOp::create(builder_, at, c, t, f);
    }

    default:                               // ExprKind::Index
      return fail(e.range, "indexing an array is E4.S7");
    }
  }

  // ---- binary operators (E4.S3) ----
  //
  // Everything here has one shape: bring both operands to a common WORKING
  // WIDTH, build the machine operation there, then narrow the result to the
  // width the type checker computed. For most rows of LANGUAGE.md's table
  // those two widths are equal and the narrowing disappears. Comparisons,
  // division, remainder and the shifts are the rows where they differ, so
  // each of those gets its own function.
  mlir::Value emitBinary(Binary& n, mlir::Location at) {
    mlir::Value l = emit(*n.lhs), r = emit(*n.rhs);
    if (!l || !r) return {};
    const Type lt = n.lhs->type, rt = n.rhs->type, res = n.type;

    switch (n.op) {

    // A comparison's operands meet at max(wa,wb) — NOT at the result width,
    // which is 1. Coercing to the result width would truncate both operands
    // to a single bit, and max3 would still pass every test.
    case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge:
    case Tok::EqEq: case Tok::BangEq: {
      const unsigned w = std::max(lt.width, rt.width);
      mlir::Value a = coerce(at, l, lt.isSigned, w);
      mlir::Value b = coerce(at, r, rt.isSigned, w);
      return mlir::arith::CmpIOp::create(builder_, at,
                                         predicate(n.op, lt.isSigned), a, b);
    }

    // Both operands are already u1 and neither side short-circuits, so these
    // are plain bitwise operations on one bit.
    case Tok::AmpAmp:   return mlir::arith::AndIOp::create(builder_, at, l, r);
    case Tok::PipePipe: return mlir::arith::OrIOp::create(builder_, at, l, r);

    case Tok::Shl: case Tok::Shr:       return emitShift(n, at, l, r);
    case Tok::Slash: case Tok::Percent: return emitDivRem(n, at, l, r);

    // Every remaining row has a result at least as wide as both operands, so
    // the working width IS the result width and nothing is narrowed after:
    //     a + b   max(wa,wb) + 1        a & b, a | b, a ^ b   max(wa,wb)
    //     a - b   max(wa,wb) + 1        a * b                 wa + wb
    default: {
      mlir::Value a = coerce(at, l, lt.isSigned, res.width);
      mlir::Value b = coerce(at, r, rt.isSigned, res.width);
      switch (n.op) {
      case Tok::Plus:  return mlir::arith::AddIOp::create(builder_, at, a, b);
      // Subtraction is always signed, even for two unsigned operands. Each
      // operand still extends by its OWN signedness, so u8(0) - u8(1) is
      // zero-extended to nine bits and subtracted into i9(-1), not 511.
      case Tok::Minus: return mlir::arith::SubIOp::create(builder_, at, a, b);
      case Tok::Star:  return mlir::arith::MulIOp::create(builder_, at, a, b);
      case Tok::Amp:   return mlir::arith::AndIOp::create(builder_, at, a, b);
      case Tok::Pipe:  return mlir::arith::OrIOp::create(builder_, at, a, b);
      case Tok::Caret: return mlir::arith::XOrIOp::create(builder_, at, a, b);
      default:         return fail(n.range, "unsupported binary operator");
      }
    }
    }
  }

  // Division and remainder, with E4.S4's guard built in.
  //
  // `arith` leaves x/0 undefined; LANGUAGE.md defines it as 0. So the guard
  // protects the DIVISOR, not the result: if a zero ever reaches arith.divsi
  // the optimiser is entitled to assume that path cannot happen and delete
  // the correction along with it.
  //
  // The working width is max(wa, wb, result) rather than the result width,
  // because these are the two rows where the result can be NARROWER than an
  // operand — `u8 / u16` is u8 and `a % b` is min(wa,wb). Truncating the
  // divisor first would turn u16(257) into u8(1) and change the answer.
  mlir::Value emitDivRem(Binary& n, mlir::Location at, mlir::Value l,
                         mlir::Value r) {
    const Type lt = n.lhs->type, rt = n.rhs->type, res = n.type;
    const unsigned w = std::max({lt.width, rt.width, res.width});

    mlir::Value a = coerce(at, l, lt.isSigned, w);
    mlir::Value b = coerce(at, r, rt.isSigned, w);

    mlir::Value zero = intConst(at, 0, w);
    mlir::Value byZero = mlir::arith::CmpIOp::create(
        builder_, at, mlir::arith::CmpIPredicate::eq, b, zero);
    mlir::Value safe = mlir::arith::SelectOp::create(
        builder_, at, byZero, intConst(at, 1, w), b);

    mlir::Value out;
    if (n.op == Tok::Slash)
      out = res.isSigned
                ? mlir::Value(mlir::arith::DivSIOp::create(builder_, at, a, safe))
                : mlir::Value(mlir::arith::DivUIOp::create(builder_, at, a, safe));
    else
      out = res.isSigned
                ? mlir::Value(mlir::arith::RemSIOp::create(builder_, at, a, safe))
                : mlir::Value(mlir::arith::RemUIOp::create(builder_, at, a, safe));

    mlir::Value guarded =
        mlir::arith::SelectOp::create(builder_, at, byZero, zero, out);
    return coerce(at, guarded, res.isSigned, res.width);
  }

  // Shifts, with E4.S4's guard built in.
  //
  // The result keeps the LEFT operand's width — shifts are the one place the
  // language drops bits without a cast. `arith` needs both operands to share
  // a type, so the amount is coerced to that width; but the out-of-range
  // test happens FIRST, on the original amount, because truncating u16(256)
  // to i8 would turn an over-wide shift into a shift by zero.
  //
  // LANGUAGE.md's runtime table for amount >= width:
  //     a << b                   -> 0
  //     a >> b, a unsigned       -> 0
  //     a >> b, a signed         -> 0 or -1, the sign of a
  // The signed case is exactly `shrsi` by width-1, so it needs a clamp and
  // no correction afterwards.
  mlir::Value emitShift(Binary& n, mlir::Location at, mlir::Value l,
                        mlir::Value r) {
    const Type lt = n.lhs->type, rt = n.rhs->type;
    const unsigned w = lt.width;                  // == n.type.width

    // Compare at a width that can hold both the amount and `w` itself: a u4
    // amount cannot be compared against 16 without room for the 16.
    const unsigned gw = std::max(rt.width, bitsFor(w));
    mlir::Value amount = coerce(at, r, /*fromSigned=*/false, gw);
    mlir::Value oob = mlir::arith::CmpIOp::create(
        builder_, at, mlir::arith::CmpIPredicate::uge, amount,
        intConst(at, w, gw));

    if (n.op == Tok::Shr && lt.isSigned) {
      mlir::Value clamped = mlir::arith::SelectOp::create(
          builder_, at, oob, intConst(at, w - 1, gw), amount);
      return mlir::arith::ShRSIOp::create(builder_, at, l,
                                          coerce(at, clamped, false, w));
    }

    // Shift by zero when out of range, so nothing undefined reaches `arith`,
    // then select the defined answer.
    mlir::Value inRange = mlir::arith::SelectOp::create(
        builder_, at, oob, intConst(at, 0, gw), amount);
    mlir::Value by = coerce(at, inRange, false, w);
    mlir::Value out =
        n.op == Tok::Shl
            ? mlir::Value(mlir::arith::ShLIOp::create(builder_, at, l, by))
            : mlir::Value(mlir::arith::ShRUIOp::create(builder_, at, l, by));
    return mlir::arith::SelectOp::create(builder_, at, oob,
                                         intConst(at, 0, w), out);
  }

  static mlir::arith::CmpIPredicate predicate(Tok op, bool isSigned) {
    using P = mlir::arith::CmpIPredicate;
    switch (op) {
    case Tok::Lt:     return isSigned ? P::slt : P::ult;
    case Tok::Le:     return isSigned ? P::sle : P::ule;
    case Tok::Gt:     return isSigned ? P::sgt : P::ugt;
    case Tok::Ge:     return isSigned ? P::sge : P::uge;
    case Tok::BangEq: return P::ne;
    default:          return P::eq;              // Tok::EqEq
    }
  }

  // ---- statements ----

  void emitBlock(Block& b) {
    for (auto& s : b.stmts) {
      emit(*s);
      if (failed_ || returned_) return;   // nothing may follow a terminator
    }
  }

  void emit(Stmt& s) {
    switch (s.kind) {

    case StmtKind::VarDecl: {
      auto& n = static_cast<VarDecl&>(s);
      if (n.initIsRead) { fail(s.range, "reading a stream is E6"); return; }
      mlir::Value v = emit(*n.init);
      if (!v) return;
      // The checker already proved this is a widening of the same signedness,
      // so the only thing left is to emit the extension it implies.
      values_[n.symbol] = coerce(loc(s.range), v, n.init->type.isSigned,
                                 n.type.width);
      // No allocation, no store. The declaration just names a value.
      return;
    }

    case StmtKind::Assign: {
      auto& n = static_cast<Assign&>(s);
      if (n.index)       { fail(s.range, "assigning to an array element is E4.S7"); return; }
      if (n.valueIsRead) { fail(s.range, "reading a stream is E6"); return; }
      mlir::Value v = emit(*n.value);
      if (!v) return;
      // Assignment REPLACES the mapping. This is SSA construction for
      // straight-line code, complete. Where two control-flow paths assign the
      // same variable, emitIf below turns the disagreement into scf.if
      // results.
      values_[n.symbol] = coerce(loc(s.range), v, n.value->type.isSigned,
                                 n.symbol->type.width);
      return;
    }

    case StmtKind::Return: {
      auto& n = static_cast<Return&>(s);
      // Two different failures, so two different branches: a `return` with
      // nothing to return is a refusal, while a null Value from emit() means
      // the operand already reported its own diagnostic.
      if (!n.value) {
        fail(s.range, "`return` with no value; every MiniHLS function "
                      "returns one");
        return;
      }
      mlir::Value v = emit(*n.value);
      if (!v) return;
      const auto at = loc(s.range);
      v = coerce(at, v, n.value->type.isSigned, returnType_.width);
      mlir::func::ReturnOp::create(builder_, at, v);
      returned_ = true;
      return;
    }

    case StmtKind::If:    emitIf(static_cast<If&>(s)); return;
    case StmtKind::Block: emitBlock(static_cast<Block&>(s)); return;

    // TODO: For (S6), ArrayDecl/Write (S7)
    default:
      fail(s.range, "this statement is not emitted yet (`for` is E4.S6, "
                    "arrays and streams are E4.S7)");
      return;
    }
  }

  // ---- if/else (E4.S5) ----

  // Every symbol assigned anywhere inside a statement. These are the values
  // that have to flow out of an `scf.if`, one result each. This set IS the
  // phi nodes a textbook SSA algorithm would insert at the merge point.
  //
  // A VarDecl is deliberately skipped: it declares a NEW symbol, which can
  // never be visible after the branch that declared it ends.
  static void collectAssigned(Stmt& s, llvm::SetVector<const Symbol*>& out) {
    switch (s.kind) {
    case StmtKind::Assign:
      if (const Symbol* sym = static_cast<Assign&>(s).symbol) out.insert(sym);
      return;
    case StmtKind::Block:
      for (auto& inner : static_cast<Block&>(s).stmts) collectAssigned(*inner, out);
      return;
    case StmtKind::If: {
      auto& n = static_cast<If&>(s);
      collectAssigned(*n.thenB, out);
      if (n.elseS) collectAssigned(*n.elseS, out);
      return;
    }
    case StmtKind::For:
      collectAssigned(*static_cast<For&>(s).body, out);
      return;
    default:
      return;
    }
  }

  void emitIf(If& n) {
    mlir::Value cond = emit(*n.cond);
    if (!cond) return;
    const auto at = loc(n.range);

    // The symbols either branch assigns become the scf.if's results.
    llvm::SetVector<const Symbol*> modified;
    collectAssigned(*n.thenB, modified);
    if (n.elseS) collectAssigned(*n.elseS, modified);

    llvm::SmallVector<const Symbol*> carried;
    llvm::SmallVector<mlir::Type> resultTypes;
    for (const Symbol* sym : modified) {
      mlir::Value prior = values_.lookup(sym);
      if (!prior) {
        fail(n.range, "'" + sym->name + "' is assigned inside this `if` but "
                      "has no value before it");
        return;
      }
      carried.push_back(sym);
      resultTypes.push_back(prior.getType());
    }

    // addThenBlock/addElseBlock give empty blocks with no terminator, which
    // is what we want — the yield below is ours to build.
    auto ifOp = mlir::scf::IfOp::create(builder_, at, resultTypes, cond,
                                        /*addThenBlock=*/true,
                                        /*addElseBlock=*/true);

    // Each branch starts from the bindings in force before the `if`. That is
    // also what handles the one-sided case for free: a symbol a branch never
    // assigns still yields something, namely the value it already had.
    const auto entry = values_;

    if (!emitBranch(ifOp.thenBlock(), n.range, carried,
                    [&] { emitBlock(*n.thenB); }))
      return;
    values_ = entry;

    if (!emitBranch(ifOp.elseBlock(), n.range, carried,
                    [&] { if (n.elseS) emit(*n.elseS); }))
      return;
    values_ = entry;

    // Past the merge, each carried symbol names the matching result.
    for (size_t i = 0; i < carried.size(); ++i)
      values_[carried[i]] = ifOp.getResult(i);
  }

  // Emit one branch into `block`, then terminate it by yielding the current
  // value of every carried symbol.
  template <typename Body>
  bool emitBranch(mlir::Block* block, Range where,
                  llvm::ArrayRef<const Symbol*> carried, Body body) {
    mlir::OpBuilder::InsertionGuard guard(builder_);
    builder_.setInsertionPointToStart(block);
    body();
    if (failed_) return false;
    if (returned_) {
      fail(where, "`return` inside an `if` is not emitted yet: an early exit "
                  "cannot be one of the values an scf.if yields");
      return false;
    }
    llvm::SmallVector<mlir::Value> yielded;
    for (const Symbol* sym : carried) yielded.push_back(values_.lookup(sym));
    mlir::scf::YieldOp::create(builder_, loc(where), yielded);
    return true;
  }

  mlir::OpBuilder builder_;
  const SourceFile& src_;
  Diagnostics& diags_;
  llvm::DenseMap<const Symbol*, mlir::Value> values_;
  llvm::DenseMap<const Symbol*, const ConstDecl*> constants_;
  Type returnType_;
  bool returned_ = false;
  bool failed_ = false;
};

} // namespace

mlir::OwningOpRef<mlir::ModuleOp> emitModule(mlir::MLIRContext& ctx, Compilation& c) {
  ctx.loadDialect<mlir::func::FuncDialect, mlir::arith::ArithDialect,
                  mlir::scf::SCFDialect>();
  return MLIRGen(ctx, *c.source, *c.diags).emit(c.program);
}

// The `emit-mlir` subcommand: front end, emission, verifier, print.
int emitMlirFile(const std::string& path) {
  Compilation c = compileFile(path);
  if (!c.source) return 1;
  c.diags->print(std::cerr);
  if (!c.ok) return 1;

  mlir::MLIRContext ctx;
  auto module = emitModule(ctx, c);
  if (!module) { c.diags->print(std::cerr); return 1; }

  // Proves the structure is sound without leaving the process - the same
  // check `| mlir-opt` runs, just earlier.
  if (mlir::failed(mlir::verify(*module))) {
    std::cerr << "minihls: emitted IR failed the verifier\n";
    return 1;
  }

  module->print(llvm::outs());        // already newline-terminated
  return 0;
}

} // namespace minihls
