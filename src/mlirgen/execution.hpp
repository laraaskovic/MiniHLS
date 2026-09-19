#pragma once

#include "interp/io.hpp"
#include "sema/sema.hpp"

#include "mlir/IR/BuiltinOps.h"

#include <memory>
#include <string>

namespace mlir {
class ExecutionEngine;
}

// The execution check: runs MLIRGen's output by lowering it to the LLVM
// dialect and JIT-compiling it, so it can be compared with the AST interpreter
// on the same inputs.
//
// The two share no code for evaluating operations -- the interpreter applies
// the width rules to source operators, while this runs the `arith` operations
// MLIRGen chose, compiled by LLVM -- so agreement between them is real
// evidence that MLIRGen is correct.
//
// To keep the C++ side independent of how LLVM lays out odd widths such as
// `i17`, the module gains a harness function that takes every input and output
// as a flat buffer of 64-bit integers and does the conversions in MLIR.
namespace minihls::mlirgen {

class Executable {
 public:
  // Clones `module`, adds the harness, lowers to LLVM, and JIT-compiles.
  // Returns null with `error` set on failure.
  static std::unique_ptr<Executable> create(mlir::ModuleOp module,
                                            const sema::FunctionInfo& function,
                                            std::string& error);
  ~Executable();

  interp::Outputs run(const interp::Inputs& inputs) const;

 private:
  Executable() = default;

  std::unique_ptr<mlir::ExecutionEngine> engine_;
  const sema::FunctionInfo* function_ = nullptr;
  std::size_t input_slots_ = 0;
  std::size_t output_slots_ = 0;
};

}  // namespace minihls::mlirgen
