// src/hwgen/hwgen.hpp — in minihls_mlir, not minihls_core
//
// E8 rung one: the purely COMBINATIONAL path to SystemVerilog.
//
// `arith` and `scf` describe a dataflow graph with no notion of time. CIRCT's
// `hw` and `comb` describe a circuit that settles within one cycle: `comb` has
// no clock and no state, so everything it can express is combinational by
// construction. For a program whose whole operator chain fits in one clock
// period, that is the entire lowering — no FSM, no registers, no start/done
// protocol, because there is nothing to sequence.
//
// What that buys: `max3` and `abs_diff` come out as real SystemVerilog this
// compiler generated, verifiable against the AST interpreter, before any of
// E7's scheduling or E8's FSM exists. Loops and arrays do NOT fit here and are
// refused with a diagnostic naming the story that lands them.
#pragma once
#include "driver/compile.hpp"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include <string>

namespace minihls {

// func/arith/scf in, hw/comb out. Null when the program is not combinational,
// with the reason in `diags`.
mlir::OwningOpRef<mlir::ModuleOp> lowerToHW(mlir::MLIRContext& ctx,
                                            mlir::ModuleOp source,
                                            Diagnostics& diags,
                                            const SourceFile& src);

// `minihls emit-hw <file.hc>` — print the hw/comb MLIR. This is the debugging
// view: when the Verilog looks wrong, the question is almost always whether
// this layer is wrong or the export is.
int emitHWFile(const std::string& path);

// `minihls emit-verilog <file.hc>` — the same lowering, run through CIRCT's
// ExportVerilog. This is the one that produces a .sv file.
int emitVerilogFile(const std::string& path);

}
