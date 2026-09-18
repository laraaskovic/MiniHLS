# MiniHLS: A High-Level Synthesis Compiler on MLIR and CIRCT

A compiler that takes functions written in a small C-like language and generates synthesizable, verified SystemVerilog. This is the same job commercial high-level synthesis tools do: turn a sequential software description into a datapath, a controller, and a schedule that meets a target clock period.

The compiler is built the way industry compilers are: on **MLIR** for its intermediate representation and optimizations, and on **CIRCT** (MLIR for hardware) for scheduling solvers and Verilog output. The parts that make it an HLS compiler are written here: the language front end, the translation into MLIR, the scheduling pass, and the generation of the controller and datapath.

> **Start with [docs/PLAN.md](docs/PLAN.md).** It is the project plan and
> learning guide: what already exists in the world, what the frameworks
> provide, what this project adds and why, how novel it is, and the
> step-by-step build order.
>
> [docs/GUIDE.md](docs/GUIDE.md) explains the hardware toolchain (Verilator,
> Yosys, Questa, Quartus) and the verification strategy.
> [docs/COMPILERS_AND_SYSTEMS.md](docs/COMPILERS_AND_SYSTEMS.md) compares
> compiling to hardware with compiling to a CPU.

## Motivation

FPGA toolchains are compilers. Synthesis lowers RTL to gates, HLS lowers C to RTL, and hardware generators like Chisel and Hardcaml raise the abstraction further. Engineers who understand both compilers and hardware are rare, and they are exactly who FPGA tool teams, hardware compiler groups, ML compiler teams, and trading firms building internal RTL generators look for. Those teams work inside LLVM and MLIR, so this project does too.

It also produces a measurable result: generated hardware can be compared directly against hand-written RTL for the same function on latency, area, and maximum frequency.

## Architecture

```
.hc source
  -> Lexer + parser          -> AST                                  ours (done)
  -> Semantic analysis       -> typed AST: widths, loop bounds       ours
  -> AST interpreter         (the golden reference model)            ours
  -> MLIRGen                 -> MLIR: func / arith / scf / memref    ours
  -> Optimization            canonicalize, CSE, SCCP, unrolling      MLIR
                             + bit-width narrowing pattern           ours
  -> Execution check         lower to LLVM, run, compare             MLIR
  -> Scheduling              ChainingProblem / ModuloProblem         ours, on CIRCT solvers
  -> Hardware generation     FSM + datapath -> hw / comb / seq       ours
  -> Verilog                 ExportVerilog                           CIRCT
  -> Co-simulation           generated Verilator testbench           ours + Verilator
  -> Reports                 Yosys for area, Quartus for ALMs/Fmax
```

The AST interpreter is built before any hardware is generated and every later stage is checked against it: the MLIR is executed through LLVM and compared, and the generated RTL is simulated and compared. When they disagree, the bug is in the stage between, which narrows debugging enormously.

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

C++20 with CMake and GoogleTest. The lexer and parser are hand-written, with no parser generator. MLIR and CIRCT provide the IR, the optimization passes, the scheduling solvers, and Verilog export. Verilator 5 handles co-simulation, Yosys gives fast area estimates, Questa provides 4-state X-propagation checks, and Quartus provides real ALM/DSP counts and timing closure. The default clock target is 6.4 ns (156.25 MHz, the 10GbE datapath clock) and is configurable.

```
minihls/
  src/
    frontend/     lexer, parser, AST, diagnostics                (done)
    sema/         type checking, width rules, scopes
    interp/       AST interpreter (golden reference)
    mlirgen/      AST -> MLIR
    dialect/      the minihls stream dialect (ODS)
    transforms/   narrowing pattern, pass pipeline
    hls/          delay model, scheduling, hardware generation
    driver/       CLI
  examples/       .hc programs
  tests/          unit, MLIR (FileCheck), cosim (Verilator), fuzz
  docs/           plan, guides
```

Planned CLI:

```
minihls emit-mlir examples/dot.hc --top dot            # software-level MLIR
minihls compile   examples/dot.hc --top dot --clock-ns 6.4 --emit-sv dot.sv --report schedule
```

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

To build under AddressSanitizer and UndefinedBehaviorSanitizer — worth doing
after any change to the tree-walking code, which is where use-after-move and
out-of-bounds bugs hide:

```bash
cmake -S . -B build-asan -DMINIHLS_SANITIZE=ON -DMINIHLS_COSIM=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
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
| MLIR, CIRCT | IR, passes, scheduling, Verilog | prebuilt CIRCT release or source build; see docs/PLAN.md step 0 |
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

- `unit` (57 tests) — the C++ side: the fixed-width integer semantics in
  [bits.hpp](src/support/bits.hpp) that every later stage depends on, the lexer,
  the parser's precedence and associativity, diagnostics, and print-then-reparse
  stability over every file in [examples/](examples/).
- `cosim` (3 tests) — Verilated hardware driven against an independent software
  model. The width-8 adder is checked exhaustively over all 65536 input pairs;
  the width-16 adder on a fixed-seed random sweep plus range corners.

Run one suite on its own with `ctest --test-dir <build> -R cosim`, or a single
case with `minihls_unit_tests --gtest_filter='RoundTrip.*'`.

## Milestones

The reasoning behind each step is in [docs/PLAN.md](docs/PLAN.md#8-step-by-step-build-plan); task checklists, effort estimates, and the results to record are in [docs/MILESTONES.md](docs/MILESTONES.md). Milestones 0 and 1 were built before the move to MLIR and carry over unchanged.

| # | Step | Status |
| - | ---- | ------ |
| 0 | Setup: build system, co-simulation harness, area reports | **done** |
| 1 | Lexer, parser, and AST | **done** |
| P0 | Toolchain: MLIR and CIRCT in WSL, hand-written module to Verilator | next |
| P1 | Read and run MLIR by hand | |
| P2 | Semantic analysis and the golden interpreter | |
| P3 | MLIRGen: AST to func/arith/scf/memref, checked by execution | |
| P4 | Optimization pipeline and the bit-width narrowing pattern | |
| P5 | The stream dialect | |
| P6 | Scheduling on CIRCT's solvers | |
| P7 | First hardware: FSM, datapath, interfaces, co-simulation | |
| P8 | Pipelining and streams | |
| P9 | Resource sharing and latency-versus-area curves | |
| P10 | Verification hardening: random programs, differential testing | |
| P11 | Capstone: ITCH parsing compared with hand-written RTL | |

A half-finished from-scratch middle end (hand-written SSA IR and passes) is kept on the `from-scratch` branch for reference.

### 0. Setup &mdash; done
CMake project with GoogleTest, a hand-written SystemVerilog adder simulated in Verilator from a C++ test, and a Yosys script reporting cell counts.
**Done when:** one command builds everything and runs both test suites.

Built: [hw/minihls_adder.sv](hw/minihls_adder.sv) with per-width wrappers,
[src/support/bits.hpp](src/support/bits.hpp) for fixed-width integer semantics,
12 unit tests, 3 co-simulation tests (width-8 exhaustive over all 65536 input
pairs), and [scripts/area_report.sh](scripts/area_report.sh) reporting 21/50/111
cells at widths 4/8/16. The harness was mutation-tested: removing the adder's
sign extension makes the co-simulation suite fail, as it must.

### 1. Lexer, parser, and AST &mdash; done
Hand-written lexer, recursive-descent parser with Pratt-style expression parsing for precedence and associativity, AST types and printer, diagnostics with source locations, and `LANGUAGE.md`.
**Done when:** every example parses, print-then-reparse is stable, and malformed programs produce clear errors.

Built: [LANGUAGE.md](LANGUAGE.md) with the full grammar and width rules;
[src/frontend/](src/frontend/) (lexer, Pratt parser, AST, printer, diagnostics);
six programs in [examples/](examples/); and a `minihls parse` driver.
Print-then-reparse stability is checked as a property over every example rather
than against golden files, so new examples are covered automatically.

```
$ minihls parse examples/dot.hc --print-ast
$ minihls parse broken.hc
broken.hc:2:14: error: expected ';' after a declaration, but found 'return'
      i32 acc = 0
                 ^
```

## Deliverables

- The number of example and fuzzed programs passing co-simulation
- Latency-versus-area trade-off curves from scheduling and binding
- Achieved II and Fmax for pipelined examples
- The capstone comparison against hand-written ITCH RTL

## References

Lattner et al., "MLIR: Scaling Compiler Infrastructure for Domain Specific Computation," CGO 2021.
The MLIR Toy tutorial, mlir.llvm.org.
The CIRCT documentation, circt.llvm.org.
Kastner, Matai, and Neuendorffer, *Parallel Programming for FPGAs* (free online).
Cong and Zhang, "An Efficient and Versatile Scheduling Algorithm Based on SDC Formulation," DAC 2006.
Canis et al., "LegUp: High-Level Synthesis for FPGA-Based Processor/Accelerator Systems," FPGA 2011.
Braun et al., "Simple and Efficient Construction of Static Single Assignment Form," CC 2013.
Cooper, Harvey, and Kennedy, "A Simple, Fast Dominance Algorithm."
De Micheli, *Synthesis and Optimization of Digital Circuits*, scheduling chapters.
Yang et al., "Finding and Understanding Bugs in C Compilers," PLDI 2011.
