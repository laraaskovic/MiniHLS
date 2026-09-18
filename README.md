# MiniHLS: A High-Level Synthesis Compiler

A compiler that takes functions written in a small C-like language and generates synthesizable, verified SystemVerilog. This is the same job commercial high-level synthesis tools do: turn a sequential software description into a datapath, a controller, and a schedule that meets a target clock period.

The core algorithms are written from scratch. LLVM, MLIR, and CIRCT are deliberately not used, since implementing scheduling, binding, and pipelining is the point of the project.

> New to the toolchain? [docs/GUIDE.md](docs/GUIDE.md) explains what the problem
> is, what Verilator, Yosys, Questa, and Quartus each do and why, how the verification
> strategy works, and what currently exists in the repository. This file is the
> specification; that one is the orientation.
>
> For background on *why* the stack looks like this — how compiling to hardware
> compares to compiling to x86 or a bytecode VM, plus the operating-system
> concepts underneath (processes, virtual memory, linking, why there's no stack
> in a circuit) — see
> [docs/COMPILERS_AND_SYSTEMS.md](docs/COMPILERS_AND_SYSTEMS.md).

## Motivation

FPGA toolchains are compilers. Synthesis lowers RTL to gates, HLS lowers C to RTL, and hardware generators like Chisel and Hardcaml raise the abstraction further. Engineers who understand both compilers and hardware are rare, and they are exactly who FPGA tool teams, hardware compiler groups, and trading firms building internal RTL generators look for.

This project also produces a measurable result: generated hardware can be compared directly against hand-written RTL for the same function on latency, area, and maximum frequency.

## Architecture

```
.hc source
  -> Lexer + parser         -> AST
  -> Semantic analysis      -> typed AST (bit widths, signedness, constant loop bounds)
  -> AST interpreter        (reference model 1)
  -> Lowering               -> CFG IR -> SSA IR
  -> IR interpreter         (reference model 2, run after every pass)
  -> Optimization           constant propagation, DCE, CSE, strength reduction,
                            bit-width range analysis, if-conversion, loop unrolling
  -> HLS middle end         delay characterization, scheduling, binding,
                            register insertion, pipelining and II analysis
  -> RTL generation         FSM + datapath, or pipelined datapath, in SystemVerilog
  -> Co-simulation          Verilator, compared against both interpreters
  -> Reports                Yosys for area, Quartus for ALMs/DSPs and Fmax
```

Two interpreters are built before any hardware is generated. They serve as trusted references: when generated RTL disagrees with them, the bug is in the hardware path, which narrows debugging enormously.

## Input language

Types are integers with explicit widths (`i8`, `u7`, `i32`), fixed-size arrays that become memories, `const` arrays that become ROMs, and `stream<T>` parameters that become ready/valid ports. Statements cover declarations, assignment, `if`/`else`, `for` loops with constant bounds, and `return`. There are no pointers, recursion, dynamic memory, or floating point. Pragmas control `pipeline II=<n>`, `unroll`, and array `partition`.

```c
// examples/dot.hc
i32 dot(i16 a[64], i16 b[64]) {
  i32 acc = 0;
  for (u7 i = 0; i < 64; i = i + 1) {
    #pragma pipeline II=1
    acc = acc + a[i] * b[i];
  }
  return acc;
}
```

Width rules are part of the design: arithmetic grows to avoid overflow (addition is the wider operand plus one bit, multiplication is the sum of widths), and assignment truncates or extends to the destination type. The full specification lives in `LANGUAGE.md`.

## Tech stack

C++20 with CMake and GoogleTest. The lexer and parser are hand-written, with no parser generator. Verilator 5 handles co-simulation, Yosys gives fast area estimates, Questa provides 4-state X-propagation checks, and Quartus provides real ALM/DSP counts and timing closure. Python drives the scripts. The default clock target is 6.4 ns (156.25 MHz, the 10GbE datapath clock) and is configurable.

```
minihls/
  src/
    frontend/     lexer, parser, AST, diagnostics
    sema/         type checking, width rules, scopes
    interp/       AST interpreter, IR interpreter
    ir/           CFG, SSA, dominators, verifier, printer
    passes/       dataflow framework and optimization passes
    hls/          delay library, scheduling, binding, registers, pipelining, FSM
    rtl/          SystemVerilog emitter
    driver/       CLI
  examples/       .hc programs with expected outputs
  tests/          unit, cosim (Verilator), fuzz
  characterize/   operator delay and area measurement scripts
  bench/          results and write-ups
```

Every IR stage has a printer and a verifier, so malformed IR fails immediately after the pass that produced it.

CLI: `minihls compile dot.hc --top dot --clock-ns 6.4 --emit-ir --emit-sv dot.sv --report schedule`

## Building and testing

The toolchain lives in WSL (Ubuntu), not on Windows. The repository stays on the
Windows filesystem and is reached from WSL through `/mnt/c`.

From Windows:

```powershell
.\build.ps1
```

From WSL or Linux directly:

```bash
bash scripts/build_and_test.sh
```

Either one configures the project, builds it, runs both test suites through
CTest, and prints a Yosys cell-count report. Useful overrides:

```bash
BUILD_TYPE=Debug bash scripts/build_and_test.sh
BUILD_DIR=./build bash scripts/build_and_test.sh   # keep the build tree in the repo
```

The build tree defaults to `~/.cache/minihls/build` rather than `./build`,
because compiling on the `/mnt/c` 9p mount is substantially slower than on the
Linux filesystem.

### Requirements

| Tool        | Used for                        | Notes                                        |
| ----------- | ------------------------------- | -------------------------------------------- |
| g++ 13+     | compiling the compiler          | C++20                                        |
| CMake 3.24+ | build system                    | `FIND_PACKAGE_ARGS` needs 3.24               |
| GoogleTest  | unit and co-simulation suites   | fetched automatically if not installed       |
| Verilator 5 | co-simulation                   | optional: configure `-DMINIHLS_COSIM=OFF`    |
| Yosys       | area reports                    | optional: `yowasp-yosys` also works, no root |
| Questa      | 4-state X-propagation checks    | optional, lab machines only                  |
| Quartus     | real ALM/DSP counts, Fmax       | optional, lab machines only (no ARM build)   |

To install the optional pieces on Ubuntu:

```bash
sudo apt-get install -y yosys
```

Without root, the WebAssembly build of Yosys works just as well and
`scripts/area_report.sh` picks it up automatically:

```bash
pip install yowasp-yosys
```

### Test suites

CTest exposes two suites, matching the two halves of the verification strategy:

- `unit` — the C++ side, including the fixed-width integer semantics in
  [bits.hpp](src/support/bits.hpp) that every later stage depends on.
- `cosim` — Verilated hardware driven against an independent software model.
  The width-8 adder is checked exhaustively over all 65536 input pairs; the
  width-16 adder is checked on a fixed-seed random sweep plus range corners.

Run one suite on its own with `ctest --test-dir <build> -R cosim`.

## Milestones

Milestones 0 through 5 are the minimum complete version: a compiler that produces verified multi-cycle hardware. Milestone 7 and Milestone 9 are what make the project distinctive.

| # | Milestone | Status |
| - | --------- | ------ |
| 0 | Setup | **done** — 2 suites, 15 tests, area report working |
| 1 | Lexer, parser, and AST | next |
| 2 | Semantic analysis and AST interpreter | |
| 3 | IR and SSA | |
| 4 | Optimization for hardware | |
| 5 | First hardware: scheduling, registers, FSM | |
| 6 | Scheduling and binding for area | |
| 7 | Pipelining and streams | |
| 8 | Verification hardening | |
| 9 | Capstone: ITCH parsing in HLS | |

### 0. Setup &mdash; done
CMake project with GoogleTest, a hand-written SystemVerilog adder simulated in Verilator from a C++ test, and a Yosys script reporting cell counts.
**Done when:** one command builds everything and runs both test suites.

Built: [hw/minihls_adder.sv](hw/minihls_adder.sv) with per-width wrappers,
[src/support/bits.hpp](src/support/bits.hpp) for fixed-width integer semantics,
12 unit tests, 3 co-simulation tests (width-8 exhaustive over all 65536 input
pairs), and [scripts/area_report.sh](scripts/area_report.sh) reporting 21/50/111
cells at widths 4/8/16. The harness was mutation-tested: removing the adder's
sign extension makes the co-simulation suite fail, as it must.

### 1. Lexer, parser, and AST
Hand-written lexer, recursive-descent parser with Pratt-style expression parsing for precedence and associativity, AST types and printer, diagnostics with source locations, and `LANGUAGE.md`.
**Done when:** every example parses, print-then-reparse is stable, and malformed programs produce clear errors.

### 2. Semantic analysis and AST interpreter
Symbol tables and scopes, type checking with the documented width rules, verification that loop bounds are compile-time constants, and an interpreter with exact bit-width semantics.
**Done when:** every example produces its expected output and invalid programs are rejected with useful messages.

### 3. IR and SSA
Lowering to a CFG IR with bit-width-typed values, dominator tree construction, SSA construction, an IR verifier and printer, and an IR interpreter. A phi node corresponds to a multiplexer in hardware.
**Done when:** the IR interpreter matches the AST interpreter on every example.

### 4. Optimization for hardware
A worklist dataflow framework, constant propagation, dead-code elimination that respects memory and stream side effects, common-subexpression elimination, strength reduction of constant multiplications into shifts and adds, bit-width range analysis that narrows values, if-conversion of small branches into selects, and unrolling of pragma-marked loops.
**Done when:** the IR interpreter still matches after every pass, with operator counts and total bit-widths reported before and after.

### 5. First hardware: scheduling, registers, FSM
An operator delay and area library measured by synthesizing individual operators at several widths; ASAP scheduling with operation chaining under the clock period; register insertion for values live across cycle boundaries; an FSM controller with start/done/idle; memory interfaces with one-cycle read latency; the SystemVerilog emitter; and co-simulation against the IR interpreter.
**Done when:** every example's RTL passes co-simulation on random inputs, synthesizes in Yosys, and meets the clock target, with latency and resources reported per example.

### 6. Scheduling and binding for area
ALAP scheduling and mobility, SDC-based scheduling where timing and resource constraints become difference constraints solved with Bellman-Ford, operator binding with resource sharing, and register binding via the left-edge algorithm.
**Done when:** a latency-versus-area trade-off curve exists for at least two examples (such as a FIR filter with one versus four multipliers) and every point passes co-simulation.

### 7. Pipelining and streams
Loop-carried dependency analysis, II computation from dependency latency and distance plus memory port limits, a pipelined datapath with per-stage valid bits and global stall, `stream<T>` ports as ready/valid, and a report of requested versus achieved II with the limiting reason.
**Done when:** `dot.hc` and a stream example reach their best achievable II, pass co-simulation under random backpressure, and have II, latency, resources, and Fmax reported.

### 8. Verification hardening
A random program generator for the language, differential testing across the AST interpreter, the IR interpreter after every pass, and Verilator RTL, plus automatic test-case reduction by delta debugging and runs under AddressSanitizer and UndefinedBehaviorSanitizer.
**Done when:** the fuzzer has run many iterations and every bug found is fixed and logged with its reduced reproducer.

### 9. Capstone: ITCH parsing in HLS
An ITCH Add Order field extractor written in the language over a `stream<u8>`, compiled with MiniHLS and compared against the equivalent hand-written RTL feed handler on latency in cycles, ALMs, registers, DSP blocks, and Fmax. Optionally add an Intel HLS Compiler version as a third data point.
**Done when:** a written comparison exists with measured numbers and an explanation of each difference.

### Stretch goals
An LP objective for SDC scheduling using HiGHS; formal equivalence checking of small generated modules with SymbiYosys; dataflow pipelines of multiple functions connected by streams; a written comparison of this IR design against CIRCT's dialects.

## Deliverables

- The number of example and fuzzed programs passing co-simulation
- Latency-versus-area trade-off curves from scheduling and binding
- Achieved II and Fmax for pipelined examples
- The capstone comparison against hand-written ITCH RTL

## References

Kastner, Matai, and Neuendorffer, *Parallel Programming for FPGAs* (free online).
Cong and Zhang, "An Efficient and Versatile Scheduling Algorithm Based on SDC Formulation," DAC 2006.
Canis et al., "LegUp: High-Level Synthesis for FPGA-Based Processor/Accelerator Systems," FPGA 2011.
Braun et al., "Simple and Efficient Construction of Static Single Assignment Form," CC 2013.
Cooper, Harvey, and Kennedy, "A Simple, Fast Dominance Algorithm."
De Micheli, *Synthesis and Optimization of Digital Circuits*, scheduling chapters.
Yang et al., "Finding and Understanding Bugs in C Compilers," PLDI 2011.
