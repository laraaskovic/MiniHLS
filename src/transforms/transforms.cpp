#include "transforms/transforms.hpp"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/SCF/Utils/Utils.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Support/TypeID.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"
#include "mlir/Transforms/Passes.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TypeSwitch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace minihls::transforms {
namespace {

// The integer width of a value, or nothing if it is not an integer (an `index`
// subscript, a memref).
std::optional<unsigned> int_width(mlir::Value value) {
  if (auto type = llvm::dyn_cast<mlir::IntegerType>(value.getType())) return type.getWidth();
  return std::nullopt;
}

// The value of a defining `arith.constant`, whatever its type. Trip counts are
// `index` constants and the narrowing pattern meets plain integers, so this
// goes through the attribute rather than a typed accessor.
std::optional<std::int64_t> constant_of(mlir::Value value) {
  auto constant = value.getDefiningOp<mlir::arith::ConstantOp>();
  if (!constant) return std::nullopt;
  if (auto attr = llvm::dyn_cast<mlir::IntegerAttr>(constant.getValue())) return attr.getInt();
  return std::nullopt;
}

// The iteration count of a loop whose bounds and step are all constants.
// MLIRGen always emits such loops -- sema has already proven the trip count --
// but a pass must not assume what it can check.
std::optional<std::int64_t> trip_count_of(mlir::scf::ForOp loop) {
  const auto lower = constant_of(loop.getLowerBound());
  const auto upper = constant_of(loop.getUpperBound());
  const auto step = constant_of(loop.getStep());
  if (!lower || !upper || !step || *step <= 0) return std::nullopt;
  if (*upper <= *lower) return 0;
  return (*upper - *lower + *step - 1) / *step;
}

// ---------------------------------------------------------------------------
// Unrolling
// ---------------------------------------------------------------------------

constexpr llvm::StringLiteral kUnrollAttr = "minihls.unroll";

struct UnrollPass : public mlir::PassWrapper<UnrollPass, mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(UnrollPass)

  llvm::StringRef getArgument() const final { return "minihls-unroll"; }
  llvm::StringRef getDescription() const final {
    return "Unroll scf.for loops marked by #pragma unroll";
  }

  void getDependentDialects(mlir::DialectRegistry& registry) const override {
    registry.insert<mlir::arith::ArithDialect, mlir::scf::SCFDialect>();
  }

  void runOnOperation() override {
    // `walk` is post-order, so an inner loop is unrolled before the outer loop
    // that contains it and the outer loop then copies bodies that are already
    // unrolled. The loops are collected first because unrolling rewrites the
    // region underneath the walk.
    llvm::SmallVector<mlir::scf::ForOp> marked;
    getOperation().walk([&](mlir::scf::ForOp loop) {
      if (loop->hasAttr(kUnrollAttr)) marked.push_back(loop);
    });

    for (mlir::scf::ForOp loop : marked) {
      const auto attr = loop->getAttrOfType<mlir::IntegerAttr>(kUnrollAttr);
      // Removed before unrolling: the clones inherit the loop's attributes, and
      // a surviving marker would make the next run unroll them all over again.
      loop->removeAttr(kUnrollAttr);
      if (!attr || attr.getInt() <= 1) continue;
      const auto factor = static_cast<std::uint64_t>(attr.getInt());

      const auto trip_count = trip_count_of(loop);
      // A factor that reaches the trip count means "unroll completely", which
      // has its own entry point: it removes the loop rather than leaving one
      // iteration of a body that can never run again.
      const bool full = trip_count && factor >= static_cast<std::uint64_t>(*trip_count);
      // Both entry points report through LogicalResult, and loopUnrollByFactor
      // returns a FailureOr that converts to one.
      const bool unroll_failed = full ? mlir::failed(mlir::loopUnrollFull(loop))
                                      : mlir::failed(mlir::loopUnrollByFactor(loop, factor));
      if (unroll_failed) {
        loop.emitWarning() << "could not unroll by " << factor << "; left as a loop";
      }
    }
  }
};

// ---------------------------------------------------------------------------
// Bit-width narrowing
// ---------------------------------------------------------------------------

// Rebuilds one binary operation at a narrower type. Each op is named
// explicitly rather than cloned generically so that the set of operations this
// pass claims to be exact for is visible in the code.
template <typename OpT>
mlir::Value rebuild(mlir::PatternRewriter& rewriter, mlir::Location at, mlir::Value lhs,
                    mlir::Value rhs) {
  return OpT::create(rewriter, at, lhs, rhs);
}

using Rebuilder = mlir::Value (*)(mlir::PatternRewriter&, mlir::Location, mlir::Value,
                                  mlir::Value);

// The operations whose low `k` bits depend only on the low `k` bits of their
// operands -- that is, the ones that are ring homomorphisms modulo 2^k.
// Addition, subtraction, and multiplication are, and so are the bitwise
// operators, which act on each bit independently.
//
// Division, remainder, and the shifts are not: the high bits of an operand
// change the low bits of the result, so narrowing them would be wrong.
// Comparisons are excluded for the same reason. This is the whole correctness
// argument for the pass, and it holds for signed and unsigned alike, because
// the truncated result is the same residue class either way.
Rebuilder rebuilder_for(mlir::Operation* op) {
  return llvm::TypeSwitch<mlir::Operation*, Rebuilder>(op)
      .Case<mlir::arith::AddIOp>([](auto) { return &rebuild<mlir::arith::AddIOp>; })
      .Case<mlir::arith::SubIOp>([](auto) { return &rebuild<mlir::arith::SubIOp>; })
      .Case<mlir::arith::MulIOp>([](auto) { return &rebuild<mlir::arith::MulIOp>; })
      .Case<mlir::arith::AndIOp>([](auto) { return &rebuild<mlir::arith::AndIOp>; })
      .Case<mlir::arith::OrIOp>([](auto) { return &rebuild<mlir::arith::OrIOp>; })
      .Case<mlir::arith::XOrIOp>([](auto) { return &rebuild<mlir::arith::XOrIOp>; })
      .Default([](auto) { return Rebuilder(nullptr); });
}

// One operand, brought down to `type`. An operand that was widened on the way
// in collapses back to its original value instead of gaining a truncation, so
// the extension MLIRGen emitted for the width rules disappears entirely --
// which is the point: `extsi` then `trunci` is free in software and a real
// wire in hardware only because the operation between them was wide.
mlir::Value narrow_operand(mlir::PatternRewriter& rewriter, mlir::Location at,
                           mlir::Value value, mlir::IntegerType type) {
  mlir::Value source;
  if (auto signed_ext = value.getDefiningOp<mlir::arith::ExtSIOp>()) {
    source = signed_ext.getIn();
  } else if (auto unsigned_ext = value.getDefiningOp<mlir::arith::ExtUIOp>()) {
    source = unsigned_ext.getIn();
  }
  if (source) {
    const auto width = int_width(source);
    if (width == type.getWidth()) return source;
    if (width && *width > type.getWidth()) {
      return mlir::arith::TruncIOp::create(rewriter, at, type, source);
    }
  }
  return mlir::arith::TruncIOp::create(rewriter, at, type, value);
}

struct NarrowBinary : public mlir::OpRewritePattern<mlir::arith::TruncIOp> {
  using OpRewritePattern::OpRewritePattern;

  mlir::LogicalResult matchAndRewrite(mlir::arith::TruncIOp trunc,
                                      mlir::PatternRewriter& rewriter) const override {
    mlir::Operation* wide = trunc.getIn().getDefiningOp();
    if (!wide) return mlir::failure();

    // Checked before anything reads an operand or a result, so that an
    // operation of another shape entirely is rejected rather than queried.
    const Rebuilder rebuilder = rebuilder_for(wide);
    if (!rebuilder) return mlir::failure();

    // Only when the wide operation exists solely to be truncated. If something
    // else reads it, the wide hardware stays and a narrow copy beside it is
    // more area, not less.
    if (!wide->getResult(0).hasOneUse()) return mlir::failure();

    auto narrow_type = llvm::dyn_cast<mlir::IntegerType>(trunc.getType());
    if (!narrow_type) return mlir::failure();

    const mlir::Location at = wide->getLoc();
    // Built where the wide operation was, so the truncated operands dominate
    // their use even when the truncation sits far below it.
    mlir::OpBuilder::InsertionGuard guard(rewriter);
    rewriter.setInsertionPoint(wide);
    const mlir::Value lhs = narrow_operand(rewriter, at, wide->getOperand(0), narrow_type);
    const mlir::Value rhs = narrow_operand(rewriter, at, wide->getOperand(1), narrow_type);

    // Overflow flags are deliberately not carried over. `nsw`/`nuw` are
    // promises that a wrap does not happen at the wide width; at the narrow
    // width they may no longer hold, and dropping them only gives the
    // optimizer less to assume.
    rewriter.replaceOp(trunc, rebuilder(rewriter, at, lhs, rhs));
    return mlir::success();
  }
};

struct NarrowArithmeticPass
    : public mlir::PassWrapper<NarrowArithmeticPass, mlir::OperationPass<mlir::ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(NarrowArithmeticPass)

  llvm::StringRef getArgument() const final { return "minihls-narrow-arithmetic"; }
  llvm::StringRef getDescription() const final {
    return "Narrow arithmetic whose result is truncated";
  }

  void getDependentDialects(mlir::DialectRegistry& registry) const override {
    registry.insert<mlir::arith::ArithDialect>();
  }

  void runOnOperation() override {
    mlir::RewritePatternSet patterns(&getContext());
    patterns.add<NarrowBinary>(&getContext());
    if (mlir::failed(mlir::applyPatternsGreedily(getOperation(), std::move(patterns)))) {
      signalPassFailure();
    }
  }
};

// ---------------------------------------------------------------------------
// Metrics
// ---------------------------------------------------------------------------

// Operations that exist in the IR but not in the circuit: a constant is a tied
// wire, and an extension or a truncation is a renaming of bits. Counting them
// would make the report move for reasons that cost no hardware.
bool is_free(mlir::Operation* op) {
  return llvm::isa<mlir::arith::ConstantOp, mlir::arith::ExtSIOp, mlir::arith::ExtUIOp,
                   mlir::arith::TruncIOp, mlir::arith::IndexCastOp,
                   mlir::arith::IndexCastUIOp>(op);
}

}  // namespace

std::unique_ptr<mlir::Pass> create_unroll_pass() { return std::make_unique<UnrollPass>(); }

std::unique_ptr<mlir::Pass> create_narrow_arithmetic_pass() {
  return std::make_unique<NarrowArithmeticPass>();
}

Metrics measure(mlir::ModuleOp module) {
  Metrics metrics;
  module.walk([&](mlir::Operation* op) {
    mlir::Dialect* dialect = op->getDialect();
    if (!dialect || dialect->getNamespace() != "arith") return;
    if (is_free(op)) return;

    ++metrics.operators[op->getName().getStringRef().str()];
    ++metrics.operator_count;
    for (const mlir::Type type : op->getResultTypes()) {
      if (auto integer = llvm::dyn_cast<mlir::IntegerType>(type)) {
        metrics.bits += integer.getWidth();
      }
    }
  });
  return metrics;
}

std::string format_report(const Metrics& before, const Metrics& after) {
  // Every operator either side ever mentions, in the stable order std::map
  // gives, so two runs of the same program produce identical reports.
  std::vector<std::string> names;
  for (const auto& [name, count] : before.operators) names.push_back(name);
  for (const auto& [name, count] : after.operators) {
    if (!before.operators.count(name)) names.push_back(name);
  }

  const auto row = [](std::string_view label, unsigned long long left,
                      unsigned long long right) {
    std::string line(label);
    line.resize(32, ' ');
    std::string first = std::to_string(left);
    std::string second = std::to_string(right);
    const long long change = static_cast<long long>(right) - static_cast<long long>(left);
    line += std::string(10 - std::min<std::size_t>(10, first.size()), ' ') + first;
    line += std::string(10 - std::min<std::size_t>(10, second.size()), ' ') + second;
    if (change != 0) line += "   " + std::string(change > 0 ? "+" : "") + std::to_string(change);
    return line + "\n";
  };

  std::string out = "operator                            before     after\n";
  out += "--------------------------------------------------------\n";
  for (const std::string& name : names) {
    const auto left = before.operators.count(name) ? before.operators.at(name) : 0u;
    const auto right = after.operators.count(name) ? after.operators.at(name) : 0u;
    out += row(name, left, right);
  }
  out += "--------------------------------------------------------\n";
  out += row("total operators", before.operator_count, after.operator_count);
  out += row("datapath bits", before.bits, after.bits);
  return out;
}

bool optimize(mlir::ModuleOp module,
              const std::function<void(std::string_view pass)>& after_pass) {
  struct Step {
    const char* name;
    std::unique_ptr<mlir::Pass> (*make)();
  };

  // Each pass runs on its own, rather than as one pipeline, so that the
  // execution check can re-run every test vector after each one. When a pass
  // breaks a program, the name of the pass is the whole bug report.
  //
  // The order matters: canonicalization folds the constants that unrolling
  // exposes, unrolling must happen before narrowing so the copies are narrowed
  // too, and sccp runs last, on the most simplified form of the program.
  const Step steps[] = {
      {"canonicalize", [] { return mlir::createCanonicalizerPass(); }},
      {"cse", [] { return mlir::createCSEPass(); }},
      {"minihls-unroll", create_unroll_pass},
      {"canonicalize", [] { return mlir::createCanonicalizerPass(); }},
      {"cse", [] { return mlir::createCSEPass(); }},
      {"minihls-narrow-arithmetic", create_narrow_arithmetic_pass},
      {"canonicalize", [] { return mlir::createCanonicalizerPass(); }},
      {"cse", [] { return mlir::createCSEPass(); }},
      {"sccp", [] { return mlir::createSCCPPass(); }},
      {"canonicalize", [] { return mlir::createCanonicalizerPass(); }},
  };

  for (const Step& step : steps) {
    mlir::PassManager manager(module.getContext());
    manager.addPass(step.make());
    // The pass manager verifies the IR after every pass, so a pass that
    // produces something invalid fails here rather than at the next stage.
    if (mlir::failed(manager.run(module))) return false;
    if (after_pass) after_pass(step.name);
  }
  return true;
}

}  // namespace minihls::transforms
