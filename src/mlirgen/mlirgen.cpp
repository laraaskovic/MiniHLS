#include "mlirgen/mlirgen.hpp"
#include <cassert>
#include "sema/symbol.hpp"             // ast.hpp only forward-declares Symbol
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/raw_ostream.h"
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

  // Your Type -> an MLIR type. Signedness is NOT carried: MLIR integers are
  // signless, exactly like a Verilog wire. It comes back in S3, in the choice
  // of operation (extsi vs extui, divsi vs divui, cmpi slt vs ult).
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

  void emitFunction(Function& fn) {
    llvm::SmallVector<mlir::Type> argTypes;
    for (Param& p : fn.params) {
      if (p.kind != ParamKind::Scalar) {
        fail(p.range, "parameter '" + p.name +
                      "': only scalar parameters are emitted in E4.S2 "
                      "(arrays arrive in S7, streams in E6)");
        return;
      }
      argTypes.push_back(typeOf(p.type));
    }

    resultType_ = typeOf(fn.returnType);
    auto type = builder_.getFunctionType(argTypes, resultType_);
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
                     "return that E4.S2 can see (a return inside `if` needs S5)");
  }

  // ---- expressions: returns the Value holding the result ----

  mlir::Value emit(Expr& e) {
    switch (e.kind) {

    case ExprKind::IntLit: {
      auto& n = static_cast<IntLit&>(e);
      // S2 (sema) already decided this literal's type. Build a constant of it.
      return constant(loc(e.range), n.value, e.type);
    }

    case ExprKind::NameRef: {
      // Reading a variable is a map lookup. No load, no memory — the value
      // is just whatever SSA value that symbol currently names.
      auto& n = static_cast<NameRef&>(e);
      if (mlir::Value v = values_.lookup(n.symbol)) return v;

      // ...unless it is a file-scope const, which has no SSA value at all
      // until a read asks for one. Emit a FRESH constant at every read and
      // do not cache it: from S5 on, the insertion point may be inside an
      // scf.if or scf.for region, and a cached value defined in that region
      // would not dominate a later use outside it. Duplicates are exactly
      // what `cse` exists to remove, so this costs nothing after E5.
      if (auto it = constants_.find(n.symbol); it != constants_.end())
        return constant(loc(e.range), it->second->value.raw, it->second->type);

      return fail(e.range, "'" + n.name + "' has no value at this point");
    }

    case ExprKind::Binary: {
      auto& n = static_cast<Binary&>(e);
      mlir::Value l = emit(*n.lhs), r = emit(*n.rhs);
      if (!l || !r) return {};
      auto at = loc(e.range);
      switch (n.op) {

      // Comparisons need a predicate AND a signedness — sema computed the
      // latter, and it is the only place the dropped `isSigned` reappears.
      case Tok::Lt: case Tok::Le: case Tok::Gt: case Tok::Ge:
      case Tok::EqEq: case Tok::BangEq: {
        if (l.getType() != r.getType())
          return fail(e.range, "comparison operands have different widths; the "
                               "extension that fixes this is E4.S3");
        return mlir::arith::CmpIOp::create(builder_, at,
                                           predicate(n.op, n.lhs->type.isSigned), l, r);
      }

      // The arithmetic and bitwise operators wait for S3. arith requires BOTH
      // OPERANDS TO HAVE THE SAME TYPE, and the width rules say i16 + i16 is
      // i17 — so every one of them needs an extension first. Emitting them
      // now would produce IR the verifier rejects.
      default:
        return fail(e.range, "this operator needs its operands extended to a "
                             "common width first, which is E4.S3");
      }
    }

    case ExprKind::Ternary: {
      auto& n = static_cast<Ternary&>(e);
      // NOT a branch — arith.select is a mux, and BOTH arms are computed.
      // That matches LANGUAGE.md (nothing short-circuits) and matches the
      // comb.mux you hand-wrote in E1.S5. `if` becomes scf.if in S5; this
      // does not.
      mlir::Value c = emit(*n.cond);
      mlir::Value t = emit(*n.thenE);
      mlir::Value f = emit(*n.elseE);
      if (!c || !t || !f) return {};
      if (t.getType() != f.getType())
        return fail(e.range, "the two arms have different widths; extending "
                             "them to match is E4.S3");
      return mlir::arith::SelectOp::create(builder_, loc(e.range), c, t, f);
    }

    // TODO: Cast  -> arith::TruncIOp / ExtSIOp / ExtUIOp, chosen by comparing
    //                the operand's width to the target's (S3 fills this in)
    // TODO: Unary -> `-a` is arith.subi(0, a); `~a` is arith.xori(a, allOnes);
    //                `!a` is arith.cmpi eq against 0
    // TODO: Index -> S7, needs memref
    default:
      return fail(e.range, "this expression is not emitted yet (casts and "
                           "unary operators are E4.S3, indexing is E4.S7)");
    }
  }

  // A literal of any width up to 128 bits. APInt takes the words low first,
  // and ignores the ones a narrow type does not need.
  mlir::Value constant(mlir::Location at, u128 raw, Type t) {
    llvm::APInt bits(t.width, {static_cast<uint64_t>(raw),
                               static_cast<uint64_t>(raw >> 64)});
    auto ty = typeOf(t);
    return mlir::arith::ConstantOp::create(builder_, at, ty,
                                           builder_.getIntegerAttr(ty, bits));
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
      if (v.getType() != typeOf(n.type)) {
        fail(s.range, "'" + n.name + "' is declared at a different width than "
                      "its initialiser; the cast that reconciles them is E4.S3");
        return;
      }
      // No allocation, no store. The declaration just names a value.
      values_[n.symbol] = v;
      return;
    }

    case StmtKind::Assign: {
      auto& n = static_cast<Assign&>(s);
      if (n.index)       { fail(s.range, "assigning to an array element is E4.S7"); return; }
      if (n.valueIsRead) { fail(s.range, "reading a stream is E6"); return; }
      mlir::Value v = emit(*n.value);
      if (!v) return;
      if (v.getType() != typeOf(n.symbol->type)) {
        fail(s.range, "'" + n.name + "' is assigned a value of a different "
                      "width; the cast that reconciles them is E4.S3");
        return;
      }
      // Assignment REPLACES the mapping. This is SSA construction for
      // straight-line code, complete. It stops working the moment two
      // control-flow paths assign the same variable — which is why S5 and
      // S6 exist.
      values_[n.symbol] = v;
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
      if (v.getType() != resultType_) {
        fail(s.range, "the returned value is not the function's result width; "
                      "the cast that reconciles them is E4.S3");
        return;
      }
      mlir::func::ReturnOp::create(builder_, loc(s.range), v);
      returned_ = true;
      return;
    }

    case StmtKind::Block: emitBlock(static_cast<Block&>(s)); return;

    // TODO: If (S5), For (S6), ArrayDecl/Write (S7)
    default:
      fail(s.range, "this statement is not emitted yet (`if` is E4.S5, `for` "
                    "is E4.S6, arrays and streams are E4.S7)");
      return;
    }
  }

  mlir::OpBuilder builder_;
  const SourceFile& src_;
  Diagnostics& diags_;
  llvm::DenseMap<const Symbol*, mlir::Value> values_;
  llvm::DenseMap<const Symbol*, const ConstDecl*> constants_;
  mlir::Type resultType_;
  bool returned_ = false;
  bool failed_ = false;
};

} // namespace

mlir::OwningOpRef<mlir::ModuleOp> emitModule(mlir::MLIRContext& ctx, Compilation& c) {
  ctx.loadDialect<mlir::func::FuncDialect, mlir::arith::ArithDialect>();
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
