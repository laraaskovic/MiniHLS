# MiniHLS

A small high-level synthesis compiler. You write a function in a C-like
language with explicit bit widths; it produces SystemVerilog you can simulate
and synthesise.

```c
// examples/max3.hc  — to be written in E2
i16 max3(i16 a, i16 b, i16 c) {
  i16 largest = a > b ? a : b;
  return largest > c ? largest : c;
}
```

A CPU compiler decides which instructions run. An HLS compiler answers four
different questions:

1. **What operators are needed?** Here: two comparators and two multiplexers.
2. **When does each one run?** That is *scheduling*. If a comparator plus a
   multiplexer takes 1.5 ns and the clock period is 6.4 ns, all four
   operations fit in one cycle — they are *chained*.
3. **What controls the sequence?** A finite state machine, plus `start` and
   `done` so the surrounding system knows when the result is ready.
4. **Can loop iterations overlap?** That is *pipelining*, and the cycles
   between iteration starts is the *initiation interval*.

Everything in this project serves those four questions.

## How it is built

On **MLIR** for the intermediate representation and optimisations, and
**CIRCT** (MLIR for hardware) for scheduling solvers and Verilog output —
because that is where compiler work actually happens now.

We write the parts that make it an HLS compiler: the language frontend, the
translation into MLIR, the scheduler, and the hardware generator. We reuse
the parts nobody should rewrite: SSA construction, constant folding, CSE,
dominator trees, scheduling solvers and Verilog emission.

```
 .hc source
    │  lexer, parser, AST                        ours
    │  semantic analysis, width rules            ours
    ▼
 typed AST ───► AST interpreter                  ours — the golden reference
    │  MLIRGen                                   ours
    ▼
 MLIR (func / arith / scf / memref)
    │  canonicalize, CSE, SCCP, unrolling        MLIR
    │  bit-width narrowing pattern               ours
    │  lower to LLVM and run it                  MLIR — differential check
    ▼
 optimised MLIR
    │  scheduling                                ours, on CIRCT's solvers
    ▼
 scheduled MLIR
    │  FSM + datapath generation                 ours
    ▼
 hw / comb / seq
    │  ExportVerilog                             CIRCT
    ▼
 design.sv ──► Verilator co-simulation + Yosys area report
```

## Status

**Rebuilding from scratch.** This repository was deliberately emptied; the
previous implementation is still in git history, and each epic in the roadmap
says what is back there.

**→ [docs/ROADMAP.md](docs/ROADMAP.md)** is the plan: twelve epics, sliced
into stories, one story per pull request. Start at E1.

## Building

Nothing to build yet. E1 adds the build system and the toolchain.

Development happens in WSL2 (Ubuntu) on a Windows host.
