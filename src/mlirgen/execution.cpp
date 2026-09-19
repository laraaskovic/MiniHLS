#include "mlirgen/execution.hpp"

#include "support/bits.hpp"

#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVMPass.h"
#include "mlir/Conversion/MemRefToLLVM/MemRefToLLVM.h"
#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/ExecutionEngine/CRunnerUtils.h"
#include "mlir/ExecutionEngine/ExecutionEngine.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Error.h"

#include <mutex>
#include <vector>

namespace minihls::mlirgen {
namespace {

using sema::Symbol;
using sema::SymbolKind;

constexpr const char* kHarness = "minihls_harness";

void initialize_llvm_once() {
  static std::once_flag once;
  std::call_once(once, [] {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
  });
}

// Adds
//   func.func @minihls_harness(%in: memref<?xi64>, %out: memref<?xi64>)
// which unpacks the parameters from %in, calls the function, and packs the
// return value and every writable array parameter into %out.
void add_harness(mlir::ModuleOp module, const sema::FunctionInfo& function,
                 std::size_t& input_slots, std::size_t& output_slots) {
  mlir::OpBuilder builder(module.getContext());
  const mlir::Location at = builder.getUnknownLoc();
  const mlir::Type i64 = builder.getI64Type();
  const auto buffer_type = mlir::MemRefType::get({mlir::ShapedType::kDynamic}, i64);

  builder.setInsertionPointToEnd(module.getBody());
  auto harness = builder.create<mlir::func::FuncOp>(
      at, kHarness, builder.getFunctionType({buffer_type, buffer_type}, {}));
  // Ask the LLVM lowering for a C-callable wrapper taking memref descriptors.
  harness->setAttr("llvm.emit_c_interface", builder.getUnitAttr());
  mlir::Block* entry = harness.addEntryBlock();
  builder.setInsertionPointToStart(entry);
  const mlir::Value in = entry->getArgument(0);
  const mlir::Value out = entry->getArgument(1);

  auto index = [&](std::int64_t value) -> mlir::Value {
    return builder.create<mlir::arith::ConstantIndexOp>(at, value);
  };
  auto narrow = [&](mlir::Value value, unsigned width) -> mlir::Value {
    if (width == 64) return value;
    return builder.create<mlir::arith::TruncIOp>(at, builder.getIntegerType(width), value);
  };
  auto widen = [&](mlir::Value value) -> mlir::Value {
    if (value.getType() == i64) return value;
    return builder.create<mlir::arith::ExtUIOp>(at, i64, value);
  };
  // Emits `for k in [0, count): body(k)`.
  auto loop = [&](std::int64_t count, auto body) {
    builder.create<mlir::scf::ForOp>(
        at, index(0), index(count), index(1), mlir::ValueRange{},
        [&](mlir::OpBuilder&, mlir::Location, mlir::Value k, mlir::ValueRange) {
          body(k);
          builder.create<mlir::scf::YieldOp>(at);
        });
  };

  llvm::SmallVector<mlir::Value> arguments;
  std::vector<mlir::Value> buffers(function.params.size());
  std::int64_t offset = 0;
  for (std::size_t p = 0; p < function.params.size(); ++p) {
    const Symbol& s = function.symbols[static_cast<std::size_t>(function.params[p])];
    if (s.kind == SymbolKind::Scalar) {
      const mlir::Value raw = builder.create<mlir::memref::LoadOp>(at, in, index(offset));
      arguments.push_back(narrow(raw, s.type.width));
      offset += 1;
      continue;
    }
    const auto size = static_cast<std::int64_t>(s.array_size);
    const auto type = mlir::MemRefType::get({size}, builder.getIntegerType(s.type.width));
    const mlir::Value array = builder.create<mlir::memref::AllocaOp>(at, type);
    const std::int64_t base = offset;
    loop(size, [&](mlir::Value k) {
      const mlir::Value slot = builder.create<mlir::arith::AddIOp>(at, k, index(base));
      const mlir::Value raw = builder.create<mlir::memref::LoadOp>(at, in, slot);
      builder.create<mlir::memref::StoreOp>(at, narrow(raw, s.type.width), array, k);
    });
    arguments.push_back(array);
    buffers[p] = array;
    offset += size;
  }
  input_slots = static_cast<std::size_t>(offset);

  auto target = module.lookupSymbol<mlir::func::FuncOp>(function.function->name);
  auto call = builder.create<mlir::func::CallOp>(at, target, arguments);

  std::int64_t out_offset = 0;
  if (call.getNumResults() == 1) {
    builder.create<mlir::memref::StoreOp>(at, widen(call.getResult(0)), out, index(0));
    out_offset = 1;
  }
  for (std::size_t p = 0; p < function.params.size(); ++p) {
    const Symbol& s = function.symbols[static_cast<std::size_t>(function.params[p])];
    if (s.kind != SymbolKind::Array || s.is_const) continue;
    const auto size = static_cast<std::int64_t>(s.array_size);
    const std::int64_t base = out_offset;
    const mlir::Value array = buffers[p];
    loop(size, [&](mlir::Value k) {
      const mlir::Value element = builder.create<mlir::memref::LoadOp>(at, array, k);
      const mlir::Value slot = builder.create<mlir::arith::AddIOp>(at, k, index(base));
      builder.create<mlir::memref::StoreOp>(at, widen(element), out, slot);
    });
    out_offset += size;
  }
  output_slots = static_cast<std::size_t>(out_offset);
  builder.create<mlir::func::ReturnOp>(at);
}

}  // namespace

Executable::~Executable() = default;

std::unique_ptr<Executable> Executable::create(mlir::ModuleOp module,
                                               const sema::FunctionInfo& function,
                                               std::string& error) {
  initialize_llvm_once();
  mlir::MLIRContext* context = module.getContext();

  mlir::DialectRegistry registry;
  mlir::registerBuiltinDialectTranslation(registry);
  mlir::registerLLVMDialectTranslation(registry);
  context->appendDialectRegistry(registry);

  auto result = std::unique_ptr<Executable>(new Executable());
  result->function_ = &function;

  // Work on a copy, so the caller's module is unchanged.
  mlir::OwningOpRef<mlir::ModuleOp> copy(module.clone());
  add_harness(*copy, function, result->input_slots_, result->output_slots_);

  // Lower one dialect at a time: structured loops and ifs to branches, then
  // each remaining dialect to `llvm`, then drop the casts the individual
  // conversions leave between one another.
  mlir::PassManager pm(context);
  pm.addPass(mlir::createSCFToControlFlowPass());
  pm.addPass(mlir::createArithToLLVMConversionPass());
  pm.addPass(mlir::createFinalizeMemRefToLLVMConversionPass());
  pm.addPass(mlir::createConvertFuncToLLVMPass());
  pm.addPass(mlir::createConvertControlFlowToLLVMPass());
  pm.addPass(mlir::createReconcileUnrealizedCastsPass());
  if (mlir::failed(pm.run(*copy))) {
    error = "lowering to the LLVM dialect failed";
    return nullptr;
  }

  mlir::ExecutionEngineOptions options;
  auto engine = mlir::ExecutionEngine::create(*copy, options);
  if (!engine) {
    error = "JIT compilation failed: " + llvm::toString(engine.takeError());
    return nullptr;
  }
  result->engine_ = std::move(*engine);
  return result;
}

interp::Outputs Executable::run(const interp::Inputs& inputs) const {
  const sema::FunctionInfo& function = *function_;
  // One spare slot each, so an empty buffer is never passed.
  std::vector<std::int64_t> in(input_slots_ + 1, 0);
  std::vector<std::int64_t> out(output_slots_ + 1, 0);

  std::size_t offset = 0;
  for (const int index : function.params) {
    const Symbol& s = function.symbols[static_cast<std::size_t>(index)];
    const auto position = static_cast<std::size_t>(s.param_index);
    if (s.kind == SymbolKind::Scalar) {
      in[offset++] = static_cast<std::int64_t>(inputs.scalars.at(position));
    } else {
      const auto& contents = inputs.arrays.at(position);
      for (std::size_t k = 0; k < s.array_size; ++k) {
        in[offset++] = static_cast<std::int64_t>(k < contents.size() ? contents[k] : 0);
      }
    }
  }

  auto descriptor = [](std::vector<std::int64_t>& data) {
    StridedMemRefType<std::int64_t, 1> result;
    result.basePtr = data.data();
    result.data = data.data();
    result.offset = 0;
    result.sizes[0] = static_cast<std::int64_t>(data.size());
    result.strides[0] = 1;
    return result;
  };
  auto in_descriptor = descriptor(in);
  auto out_descriptor = descriptor(out);

  interp::Outputs outputs;
  if (llvm::Error failure =
          engine_->invoke(kHarness, &in_descriptor, &out_descriptor)) {
    outputs.error = "execution failed: " + llvm::toString(std::move(failure));
    return outputs;
  }

  const std::size_t params = function.params.size();
  outputs.arrays.resize(params);
  outputs.streams.resize(params);
  outputs.consumed.assign(params, 0);
  std::size_t out_offset = 0;
  if (!function.returns_void) {
    outputs.return_value =
        truncate(static_cast<std::uint64_t>(out[0]), function.return_type.width);
    out_offset = 1;
  }
  for (const int index : function.params) {
    const Symbol& s = function.symbols[static_cast<std::size_t>(index)];
    if (s.kind != SymbolKind::Array || s.is_const) continue;
    auto& contents = outputs.arrays[static_cast<std::size_t>(s.param_index)];
    for (std::size_t k = 0; k < s.array_size; ++k) {
      contents.push_back(truncate(static_cast<std::uint64_t>(out[out_offset++]), s.type.width));
    }
  }
  return outputs;
}

}  // namespace minihls::mlirgen
