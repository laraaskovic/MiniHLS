#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/Parser/Parser.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Seq/SeqDialect.h"
#include <iostream>

namespace minihls {
int dumpMlirFile(const std::string& path) {
  mlir::DialectRegistry registry;
  registry.insert<mlir::func::FuncDialect, mlir::arith::ArithDialect,
                  mlir::scf::SCFDialect, mlir::memref::MemRefDialect,
                  circt::hw::HWDialect, circt::comb::CombDialect,
                  circt::seq::SeqDialect>();

  mlir::MLIRContext ctx(registry);
  ctx.loadAllAvailableDialects();

  mlir::OwningOpRef<mlir::ModuleOp> mod =
      mlir::parseSourceFile<mlir::ModuleOp>(path, &ctx);
  if (!mod) { std::cerr << "failed to parse " << path << "\n"; return 1; }

  mod->print(llvm::outs());
  return 0;
}
}