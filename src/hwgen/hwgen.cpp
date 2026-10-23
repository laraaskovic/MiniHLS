#include "hwgen/hwgen.hpp"
#include "mlirgen/mlirgen.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

#include "circt/Conversion/ExportVerilog.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"

#include <iostream>

namespace minihls {
namespace {

using mlir::Value;

// ---------------------------------------------------------------------------
// The lowering.
//
// One pass over the function body in order. Every `arith` result is mapped to
// the `comb` value that replaces it, so by the time `func.return` is reached
// the returned value already has a hw/comb equivalent.
//
// The interesting case is `scf.if`. There is no `comb` operation for a branch,
// because `comb` has no control flow — so an `scf.if` is lowered by HOISTING
// both regions into the parent block and muxing the yielded values. Both arms
// are then computed unconditionally, which is correct here and not a
// compromise: LANGUAGE.md already says nothing short-circuits, precisely
// because in hardware both arms are wires into a mux and are computed whether
// or not they are selected. The interpreter evaluates one arm because it has
// the condition's value; this stage does not and cannot.
// ---------------------------------------------------------------------------
class HWGen {
public:
  HWGen(mlir::OpBuilder& builder, Diagnostics& diags, const SourceFile& src)
      : builder_(builder), diags_(diags), src_(src) {}

  bool run(mlir::func::FuncOp fn, mlir::ModuleOp into);

private:
  // Every refusal goes through here, so an unsupported operation produces a
  // diagnostic and no module — never half a circuit that fails the verifier.
  bool fail(mlir::Location at, const std::string& message) {
    diags_.error(rangeOf(at), message);
    failed_ = true;
    return false;
  }

  // MLIRGen attached a FileLineColLoc to every operation it built, so a
  // refusal here can still point at the line of .hc that caused it. Offsets
  // are what Diagnostics wants, and SourceFile only converts the other way,
  // so walk the line starts once. A diagnostic is not a hot path.
  Range rangeOf(mlir::Location at) {
    Range r{};
    auto flc = mlir::dyn_cast<mlir::FileLineColLoc>(at);
    if (!flc) return r;
    const unsigned wantLine = flc.getLine(), wantCol = flc.getColumn();
    const std::string& text = src_.text();
    unsigned line = 1, col = 1;
    for (uint32_t off = 0; off < text.size(); ++off) {
      if (line == wantLine && col == wantCol) { r.begin.offset = off; r.end = r.begin; return r; }
      if (text[off] == '\n') { ++line; col = 1; } else { ++col; }
    }
    return r;
  }

  Value mapped(Value v, mlir::Location at) {
    if (Value out = map_.lookup(v)) return out;
    fail(at, "internal: a value reached hwgen with no lowered equivalent");
    return {};
  }

  unsigned widthOf(mlir::Type t) { return t.getIntOrFloatBitWidth(); }

  Value hwConst(mlir::Location at, const llvm::APInt& bits) {
    return circt::hw::ConstantOp::create(builder_, at, bits);
  }

  // The three width adjustments, built from primitives that every version of
  // the comb dialect has. `comb` carries no signedness in its types either, so
  // as in MLIRGen the choice of operation is where it lives: sign extension
  // replicates the top bit, zero extension concatenates zeros, and a narrowing
  // is comb.extract, which costs nothing at all because it is just a decision
  // to stop routing the high wires.
  Value sext(mlir::Location at, Value v, unsigned to) {
    const unsigned from = widthOf(v.getType());
    if (from == to) return v;
    Value top = circt::comb::ExtractOp::create(builder_, at,
                                               builder_.getIntegerType(1), v,
                                               /*lowBit=*/from - 1);
    Value fill = circt::comb::ReplicateOp::create(
        builder_, at, builder_.getIntegerType(to - from), top);
    return circt::comb::ConcatOp::create(builder_, at, mlir::ValueRange{fill, v});
  }
  Value zext(mlir::Location at, Value v, unsigned to) {
    const unsigned from = widthOf(v.getType());
    if (from == to) return v;
    Value zeros = hwConst(at, llvm::APInt(to - from, 0));
    return circt::comb::ConcatOp::create(builder_, at, mlir::ValueRange{zeros, v});
  }
  Value trunc(mlir::Location at, Value v, unsigned to) {
    if (widthOf(v.getType()) == to) return v;
    return circt::comb::ExtractOp::create(builder_, at, builder_.getIntegerType(to),
                                          v, /*lowBit=*/0);
  }

  static circt::comb::ICmpPredicate combPred(mlir::arith::CmpIPredicate p) {
    using A = mlir::arith::CmpIPredicate;
    using C = circt::comb::ICmpPredicate;
    switch (p) {
    case A::eq:  return C::eq;
    case A::ne:  return C::ne;
    case A::slt: return C::slt;
    case A::sle: return C::sle;
    case A::sgt: return C::sgt;
    case A::sge: return C::sge;
    case A::ult: return C::ult;
    case A::ule: return C::ule;
    case A::ugt: return C::ugt;
    case A::uge: return C::uge;
    }
    return C::eq;
  }

  bool lowerBlock(mlir::Block& block);
  bool lowerOp(mlir::Operation& op);
  bool lowerIf(mlir::scf::IfOp ifOp);

  mlir::OpBuilder& builder_;
  Diagnostics& diags_;
  const SourceFile& src_;
  llvm::DenseMap<Value, Value> map_;   // arith/scf value -> hw/comb value
  Value returned_;                     // what func.return carried
  bool failed_ = false;
};

// ---------------------------------------------------------------------------

bool HWGen::lowerBlock(mlir::Block& block) {
  for (mlir::Operation& op : block) {
    if (!lowerOp(op)) return false;
  }
  return true;
}

bool HWGen::lowerIf(mlir::scf::IfOp ifOp) {
  Value cond = mapped(ifOp.getCondition(), ifOp.getLoc());
  if (!cond) return false;

  // Hoist the then region, then the else region, into the module body. The
  // builder's insertion point is already there, so "hoisting" is just lowering
  // the region's operations where we already are.
  mlir::Block* thenBlock = ifOp.thenBlock();
  mlir::Block* elseBlock = ifOp.elseBlock();
  if (!thenBlock || !elseBlock)
    return fail(ifOp.getLoc(),
                "an `if` with results must have both regions; MLIRGen always "
                "builds both, so this is an internal error");

  auto thenYield = mlir::cast<mlir::scf::YieldOp>(thenBlock->getTerminator());
  auto elseYield = mlir::cast<mlir::scf::YieldOp>(elseBlock->getTerminator());

  // The yields are terminators, so lower everything before them and read the
  // yielded values out of the map afterwards.
  for (mlir::Operation& op : thenBlock->without_terminator())
    if (!lowerOp(op)) return false;
  llvm::SmallVector<Value> thenVals;
  for (Value v : thenYield.getResults()) {
    Value lowered = mapped(v, ifOp.getLoc());
    if (!lowered) return false;
    thenVals.push_back(lowered);
  }

  for (mlir::Operation& op : elseBlock->without_terminator())
    if (!lowerOp(op)) return false;
  llvm::SmallVector<Value> elseVals;
  for (Value v : elseYield.getResults()) {
    Value lowered = mapped(v, ifOp.getLoc());
    if (!lowered) return false;
    elseVals.push_back(lowered);
  }

  // One mux per result. This is the phi node the `if` stood for, and it is now
  // literally a multiplexer. comb.mux takes the TRUE value first.
  for (size_t i = 0; i < thenVals.size(); ++i)
    map_[ifOp.getResult(i)] = circt::comb::MuxOp::create(
        builder_, ifOp.getLoc(), cond, thenVals[i], elseVals[i]);
  return true;
}

bool HWGen::lowerOp(mlir::Operation& op) {
  const auto at = op.getLoc();

  // Two operands, already the same width — arith required that, so comb gets
  // it for free and no adjustment is needed on this path.
  auto bin2 = [&](auto make) {
    Value a = mapped(op.getOperand(0), at), b = mapped(op.getOperand(1), at);
    if (!a || !b) return false;
    map_[op.getResult(0)] = make(a, b);
    return true;
  };

  if (auto c = mlir::dyn_cast<mlir::arith::ConstantOp>(op)) {
    auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(c.getValue());
    if (!intAttr)
      return fail(at, "only integer constants are lowered to hardware");
    map_[c.getResult()] = hwConst(at, intAttr.getValue());
    return true;
  }

  // ---- the datapath, one row per operation ----
  //
  // The variadic comb ops take a `twoState` flag, and it has to be passed
  // explicitly: both of their create() overloads default their fourth
  // argument, so three arguments is ambiguous. false is four-state semantics —
  // the operands may carry X and Z, as Verilog's own operators do. Setting it
  // would be a promise that no X can ever reach this operator, which nothing
  // in this compiler establishes, and it is a promise a simulator is entitled
  // to act on.

  if (mlir::isa<mlir::arith::AddIOp>(op))
    return bin2([&](Value a, Value b) {
      return circt::comb::AddOp::create(builder_, at, mlir::ValueRange{a, b},
                                        /*twoState=*/false); });
  if (mlir::isa<mlir::arith::SubIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::SubOp::create(builder_, at, a, b); });
  if (mlir::isa<mlir::arith::MulIOp>(op))
    return bin2([&](Value a, Value b) {
      return circt::comb::MulOp::create(builder_, at, mlir::ValueRange{a, b},
                                        /*twoState=*/false); });
  if (mlir::isa<mlir::arith::DivSIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::DivSOp::create(builder_, at, a, b); });
  if (mlir::isa<mlir::arith::DivUIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::DivUOp::create(builder_, at, a, b); });
  if (mlir::isa<mlir::arith::RemSIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::ModSOp::create(builder_, at, a, b); });
  if (mlir::isa<mlir::arith::RemUIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::ModUOp::create(builder_, at, a, b); });
  if (mlir::isa<mlir::arith::AndIOp>(op))
    return bin2([&](Value a, Value b) {
      return circt::comb::AndOp::create(builder_, at, mlir::ValueRange{a, b},
                                        /*twoState=*/false); });
  if (mlir::isa<mlir::arith::OrIOp>(op))
    return bin2([&](Value a, Value b) {
      return circt::comb::OrOp::create(builder_, at, mlir::ValueRange{a, b},
                                        /*twoState=*/false); });
  if (mlir::isa<mlir::arith::XOrIOp>(op))
    return bin2([&](Value a, Value b) {
      return circt::comb::XorOp::create(builder_, at, mlir::ValueRange{a, b},
                                        /*twoState=*/false); });
  if (mlir::isa<mlir::arith::ShLIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::ShlOp::create(builder_, at, a, b); });
  if (mlir::isa<mlir::arith::ShRSIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::ShrSOp::create(builder_, at, a, b); });
  if (mlir::isa<mlir::arith::ShRUIOp>(op))
    return bin2([&](Value a, Value b) { return circt::comb::ShrUOp::create(builder_, at, a, b); });

  if (auto cmp = mlir::dyn_cast<mlir::arith::CmpIOp>(op)) {
    Value a = mapped(cmp.getLhs(), at), b = mapped(cmp.getRhs(), at);
    if (!a || !b) return false;
    map_[cmp.getResult()] = circt::comb::ICmpOp::create(
        builder_, at, combPred(cmp.getPredicate()), a, b);
    return true;
  }

  // arith.select and comb.mux are the same operation under two names, and both
  // are the hardware multiplexer `?:` became in MLIRGen.
  if (auto sel = mlir::dyn_cast<mlir::arith::SelectOp>(op)) {
    Value c = mapped(sel.getCondition(), at);
    Value t = mapped(sel.getTrueValue(), at), f = mapped(sel.getFalseValue(), at);
    if (!c || !t || !f) return false;
    map_[sel.getResult()] = circt::comb::MuxOp::create(builder_, at, c, t, f);
    return true;
  }

  // ---- the width adjustments ----
  if (auto e = mlir::dyn_cast<mlir::arith::ExtSIOp>(op)) {
    Value v = mapped(e.getIn(), at);
    if (!v) return false;
    map_[e.getResult()] = sext(at, v, widthOf(e.getType()));
    return true;
  }
  if (auto e = mlir::dyn_cast<mlir::arith::ExtUIOp>(op)) {
    Value v = mapped(e.getIn(), at);
    if (!v) return false;
    map_[e.getResult()] = zext(at, v, widthOf(e.getType()));
    return true;
  }
  if (auto t = mlir::dyn_cast<mlir::arith::TruncIOp>(op)) {
    Value v = mapped(t.getIn(), at);
    if (!v) return false;
    map_[t.getResult()] = trunc(at, v, widthOf(t.getType()));
    return true;
  }

  // ---- control flow ----
  if (auto ifOp = mlir::dyn_cast<mlir::scf::IfOp>(op)) return lowerIf(ifOp);

  if (auto ret = mlir::dyn_cast<mlir::func::ReturnOp>(op)) {
    if (ret.getNumOperands() != 1)
      return fail(at, "internal: every MiniHLS function returns exactly one value");
    returned_ = mapped(ret.getOperand(0), at);
    return returned_ ? true : false;
  }

  // Anything left is either a later story or genuinely not combinational. The
  // message names which, because "unsupported operation" sends you reading the
  // wrong file.
  if (mlir::isa<mlir::scf::ForOp>(op))
    return fail(at, "a loop cannot be purely combinational: it needs a cycle "
                    "count, so it needs E7 scheduling and E8's FSM");
  return fail(at, "operation '" + op.getName().getStringRef().str() +
                  "' has no combinational lowering yet");
}

// Build the module's ports from the function signature.
//
// NOTE: this is the API in CIRCT most likely to differ from the version you
// have — hw::PortInfo and hw::ModulePortInfo have both been reshaped more than
// once. It is isolated in this one function on purpose: if the build fails,
// this is the only place to fix. `grep -n "struct PortInfo" -A 12
// $CIRCT/include/circt/Dialect/HW/PortImplementation.h` shows the shape your
// headers expect.
circt::hw::ModulePortInfo portsFor(mlir::OpBuilder& builder,
                                   mlir::func::FuncOp fn,
                                   llvm::ArrayRef<std::string> argNames) {
  llvm::SmallVector<circt::hw::PortInfo> ports;
  auto type = fn.getFunctionType();
  for (unsigned i = 0; i < type.getNumInputs(); ++i) {
    circt::hw::PortInfo p;
    p.name = builder.getStringAttr(argNames[i]);
    p.type = type.getInput(i);
    p.dir = circt::hw::ModulePort::Direction::Input;
    p.argNum = i;
    ports.push_back(p);
  }
  {
    circt::hw::PortInfo p;
    p.name = builder.getStringAttr("result");
    p.type = type.getResult(0);
    p.dir = circt::hw::ModulePort::Direction::Output;
    p.argNum = 0;
    ports.push_back(p);
  }
  return circt::hw::ModulePortInfo(ports);
}

bool HWGen::run(mlir::func::FuncOp fn, mlir::ModuleOp into) {
  if (fn.getFunctionType().getNumResults() != 1)
    return fail(fn.getLoc(), "internal: every MiniHLS function returns one value");

  // Port names come from the .hc parameter names, so the generated Verilog
  // reads like the source rather than like %arg0.
  llvm::SmallVector<std::string> argNames;
  for (unsigned i = 0; i < fn.getNumArguments(); ++i) {
    auto attr = fn.getArgAttrOfType<mlir::StringAttr>(i, "minihls.name");
    argNames.push_back(attr ? attr.getValue().str() : ("in" + std::to_string(i)));
  }

  builder_.setInsertionPointToEnd(into.getBody());
  auto ports = portsFor(builder_, fn, argNames);
  auto hwModule = circt::hw::HWModuleOp::create(
      builder_, fn.getLoc(), builder_.getStringAttr(fn.getName()), ports);

  mlir::Block* body = hwModule.getBodyBlock();
  auto outputOp = mlir::cast<circt::hw::OutputOp>(body->getTerminator());

  // Everything we build goes before the terminator, which HWModuleOp created
  // for us with no operands.
  builder_.setInsertionPoint(outputOp);

  // The func's block arguments become the module's input ports. This is the
  // same act as MLIRGen binding parameters into values_, one layer down.
  for (unsigned i = 0; i < fn.getNumArguments(); ++i)
    map_[fn.getArgument(i)] = body->getArgument(i);

  if (!lowerBlock(fn.getBody().front())) return false;
  if (!returned_)
    return fail(fn.getLoc(), "internal: no value reached hw.output");

  outputOp->setOperands(mlir::ValueRange{returned_});
  return !failed_;
}

} // namespace

// ---------------------------------------------------------------------------

mlir::OwningOpRef<mlir::ModuleOp> lowerToHW(mlir::MLIRContext& ctx,
                                            mlir::ModuleOp source,
                                            Diagnostics& diags,
                                            const SourceFile& src) {
  ctx.loadDialect<circt::hw::HWDialect, circt::comb::CombDialect>();

  mlir::OpBuilder builder(&ctx);
  mlir::OwningOpRef<mlir::ModuleOp> out =
      mlir::ModuleOp::create(builder.getUnknownLoc());

  bool sawFunction = false;
  for (auto fn : source.getOps<mlir::func::FuncOp>()) {
    sawFunction = true;
    HWGen gen(builder, diags, src);
    if (!gen.run(fn, *out)) return {};
  }
  if (!sawFunction) {
    diags.error(Range{}, "internal: no function to lower");
    return {};
  }
  return out;
}

// Shared front half of both subcommands: compile, emit arith, lower to hw.
static mlir::OwningOpRef<mlir::ModuleOp>
buildHW(mlir::MLIRContext& ctx, Compilation& c, const std::string& path) {
  auto arith = emitModule(ctx, c);
  if (!arith) { c.diags->print(std::cerr); return {}; }
  auto hw = lowerToHW(ctx, *arith, *c.diags, *c.source);
  if (!hw) { c.diags->print(std::cerr); return {}; }
  if (mlir::failed(mlir::verify(*hw))) {
    std::cerr << "minihls: " << path << ": lowered hw/comb failed the verifier\n";
    return {};
  }
  return hw;
}

int emitHWFile(const std::string& path) {
  Compilation c = compileFile(path);
  if (!c.source) return 1;
  c.diags->print(std::cerr);
  if (!c.ok) return 1;

  mlir::MLIRContext ctx;
  auto hw = buildHW(ctx, c, path);
  if (!hw) return 1;
  hw->print(llvm::outs());
  return 0;
}

int emitVerilogFile(const std::string& path) {
  Compilation c = compileFile(path);
  if (!c.source) return 1;
  c.diags->print(std::cerr);
  if (!c.ok) return 1;

  mlir::MLIRContext ctx;
  auto hw = buildHW(ctx, c, path);
  if (!hw) return 1;

  // CIRCT writes the SystemVerilog. Hand-rolling a Verilog printer is the part
  // of this project with the least to learn in it and the most tedium, and
  // getting `always_comb` sensitivity and signedness right by hand is exactly
  // the sort of thing a library should do.
  if (mlir::failed(circt::exportVerilog(*hw, llvm::outs()))) {
    std::cerr << "minihls: " << path << ": ExportVerilog failed\n";
    return 1;
  }
  return 0;
}

} // namespace minihls
