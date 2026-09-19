// Proves the build links against MLIR and CIRCT: build a tiny module with the
// C++ API, verify it, and print it.
//
// Every later milestone uses exactly these pieces -- an MLIRContext that owns
// types and dialects, an OpBuilder that creates operations, and the verifier
// that checks the result -- so this is the smallest possible use of each.

#include "circt/Dialect/Comb/CombDialect.h"
#include "circt/Dialect/Comb/CombOps.h"
#include "circt/Dialect/HW/HWDialect.h"
#include "circt/Dialect/HW/HWOps.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"

#include <gtest/gtest.h>

#include <string>

namespace {

// func.func @add(%a: i17, %b: i17) -> i17 { return a + b }
TEST(MlirSmoke, BuildsAndVerifiesAFunction) {
  mlir::MLIRContext context;
  context.loadDialect<mlir::func::FuncDialect, mlir::arith::ArithDialect>();

  mlir::OpBuilder builder(&context);
  const mlir::Location location = builder.getUnknownLoc();
  auto module = mlir::ModuleOp::create(location);
  builder.setInsertionPointToEnd(module.getBody());

  // Integers of any width are ordinary MLIR types.
  const mlir::Type i17 = builder.getIntegerType(17);
  auto function = builder.create<mlir::func::FuncOp>(
      location, "add", builder.getFunctionType({i17, i17}, {i17}));
  mlir::Block* entry = function.addEntryBlock();
  builder.setInsertionPointToStart(entry);
  mlir::Value sum =
      builder.create<mlir::arith::AddIOp>(location, entry->getArgument(0), entry->getArgument(1));
  builder.create<mlir::func::ReturnOp>(location, sum);

  ASSERT_TRUE(mlir::succeeded(mlir::verify(module)));
  std::string text;
  llvm::raw_string_ostream stream(text);
  module.print(stream);
  EXPECT_NE(text.find("arith.addi"), std::string::npos) << text;
  module.erase();
}

// hw.module @inverter(in %x : i1, out y : i1) with a CIRCT comb op inside.
TEST(MlirSmoke, BuildsAHardwareModule) {
  mlir::MLIRContext context;
  context.loadDialect<circt::hw::HWDialect, circt::comb::CombDialect>();

  mlir::OpBuilder builder(&context);
  const mlir::Location location = builder.getUnknownLoc();
  auto module = mlir::ModuleOp::create(location);
  builder.setInsertionPointToEnd(module.getBody());

  const mlir::Type i1 = builder.getI1Type();
  llvm::SmallVector<circt::hw::PortInfo> ports;
  ports.push_back({{builder.getStringAttr("x"), i1, circt::hw::ModulePort::Direction::Input}});
  ports.push_back({{builder.getStringAttr("y"), i1, circt::hw::ModulePort::Direction::Output}});
  auto hw_module = builder.create<circt::hw::HWModuleOp>(
      location, builder.getStringAttr("inverter"), ports);

  builder.setInsertionPointToStart(hw_module.getBodyBlock());
  mlir::Value input = hw_module.getBodyBlock()->getArgument(0);
  mlir::Value ones = builder.create<circt::hw::ConstantOp>(location, i1, 1);
  mlir::Value inverted = builder.create<circt::comb::XorOp>(location, input, ones);
  // The module was created with an empty terminator; point it at the result.
  hw_module.getBodyBlock()->getTerminator()->setOperands(inverted);

  ASSERT_TRUE(mlir::succeeded(mlir::verify(module)));
  std::string text;
  llvm::raw_string_ostream stream(text);
  module.print(stream);
  EXPECT_NE(text.find("comb.xor"), std::string::npos) << text;
  module.erase();
}

}  // namespace
