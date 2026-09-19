#pragma once

#include "frontend/diagnostics.hpp"
#include "frontend/source.hpp"
#include "sema/sema.hpp"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"

// MLIRGen: translates a checked function into MLIR's software-level dialects.
//
//   scalars and arithmetic  -> arith   (signless integers of any width)
//   if / else               -> scf.if  (yielding every variable a branch assigns)
//   for                     -> scf.for (over the trip count, variables as iter_args)
//   arrays                  -> memref  (parameters as arguments, local arrays
//                                       as stack buffers, const arrays as
//                                       constant globals)
//   functions               -> func
//
// Two places need care. First, the width rules: each source operator becomes
// explicit extensions to the result width followed by an operation at that
// width, so the IR spells out every bit the language implies. Second, where
// MLIR leaves behaviour undefined -- division by zero, the overflowing signed
// quotient, shifts by at least the width -- the language defines it, so the
// generated code selects the defined result explicitly (LANGUAGE.md, "Runtime
// behaviour").
namespace minihls::mlirgen {

// Loads every dialect MLIRGen emits into `context`.
void load_dialects(mlir::MLIRContext& context);

// Generates a module holding `func.func @<name>`. Returns null, with errors
// reported to `diagnostics`, for constructs not supported yet.
mlir::OwningOpRef<mlir::ModuleOp> generate(mlir::MLIRContext& context,
                                           const sema::FunctionInfo& function,
                                           const SourceFile& file,
                                           DiagnosticEngine& diagnostics);

}  // namespace minihls::mlirgen
