#pragma once

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

// The optimization pipeline (milestone P4).
//
// Most of it is MLIR's own passes -- canonicalization, common-subexpression
// elimination, sparse conditional constant propagation -- plus two of ours:
// unrolling loops marked `#pragma unroll`, and narrowing arithmetic whose
// result is truncated.
namespace minihls::transforms {

// Unrolls every scf.for carrying a `minihls.unroll` attribute, by the factor it
// names; a factor equal to the trip count unrolls completely. Innermost loops
// go first, so an unrolled outer loop copies already-unrolled inner ones.
std::unique_ptr<mlir::Pass> create_unroll_pass();

// Narrows `trunci(op(a, b))` to `op(trunci a, trunci b)` for add, subtract,
// multiply, and the bitwise operators. The low bits of those results depend
// only on the low bits of their operands, so this is exact, and it removes
// hardware: the dot product's i33 accumulator add becomes an i32 add. An
// extension feeding the narrowed operation is folded away where possible.
std::unique_ptr<mlir::Pass> create_narrow_arithmetic_pass();

// Operator counts and total result bits of the `arith` operations in a module.
// Constants and width conversions are not hardware and are not counted.
struct Metrics {
  std::map<std::string, unsigned> operators;
  unsigned operator_count = 0;
  unsigned long long bits = 0;
};
Metrics measure(mlir::ModuleOp module);
std::string format_report(const Metrics& before, const Metrics& after);

// Runs the whole pipeline. `after_pass`, when given, is called with each
// pass's name after it runs -- the execution check uses this to re-run every
// test vector after every pass. Returns false if a pass failed.
bool optimize(mlir::ModuleOp module,
              const std::function<void(std::string_view pass)>& after_pass = nullptr);

}  // namespace minihls::transforms
