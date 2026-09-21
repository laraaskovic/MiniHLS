// src/mlirgen/mlirgen.hpp — in minihls_mlir, not minihls_core
#pragma once
#include "driver/compile.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"

namespace minihls {

// The emitter proper: an already-checked Compilation in, a module out. Null
// when emission refused something, with the reason in `c.diags`.
mlir::OwningOpRef<mlir::ModuleOp> emitModule(mlir::MLIRContext& ctx, Compilation& c);

// The `emit-mlir` subcommand: compile, emit, verify, print. Returns a
// process exit code.
int emitMlirFile(const std::string& path);

}