# MiniHLS on MLIR and CIRCT: project plan and learning guide

This document explains the new direction of the project: what is being built,
what already exists in the world, which parts come from frameworks and which
parts we write ourselves (and why), how new the idea is, and the step-by-step
order in which we will build it. It is written to be learned from, not just
followed — each step says what concept it teaches and what interview question
it prepares you to answer.

- [1. Why the project changed direction](#1-why-the-project-changed-direction)
- [2. What an HLS compiler does, in one page](#2-what-an-hls-compiler-does-in-one-page)
- [3. What already exists in the world](#3-what-already-exists-in-the-world)
- [4. The frameworks, explained](#4-the-frameworks-explained)
- [5. The new pipeline: who provides each stage](#5-the-new-pipeline-who-provides-each-stage)
- [6. What we write ourselves, and why](#6-what-we-write-ourselves-and-why)
- [7. Is this novel?](#7-is-this-novel)
- [8. Step-by-step build plan](#8-step-by-step-build-plan)
- [9. What happens to the existing repository](#9-what-happens-to-the-existing-repository)
- [10. Reading list](#10-reading-list)
- [11. Glossary](#11-glossary)

---

## 1. Why the project changed direction

The first plan was to write every stage of the compiler by hand: our own IR,
our own SSA construction, our own optimizer, our own scheduler. That is a fine
way to learn the theory, but it is not how compiler work is done in industry.
Almost every production compiler team works *inside* a framework:

| Area | Framework in use |
| ---- | ---------------- |
| CPU and GPU compilers (Clang, Rust, Swift, NVIDIA, AMD) | LLVM |
| ML compilers (Triton, IREE, XLA, Mojo) | MLIR |
| HLS tools (AMD Vitis HLS, Intel HLS) | LLVM |
| Newer hardware compilers (CIRCT, AMD AI Engine tools) | MLIR |

The day-to-day job is writing passes, dialects, and lowerings in those
frameworks. Interviews still test the fundamentals — SSA, dataflow analysis,
scheduling — but they expect you to understand them, not to have rebuilt the
infrastructure underneath.

So the project now uses **MLIR** as its IR and optimization framework and
**CIRCT** (MLIR for hardware) for scheduling support and Verilog output. We
still write the parts that make it an HLS compiler: the language frontend, the
translation into MLIR, the scheduling pass, and the generation of the
controller and datapath. Those are the parts you will be asked about.

The math does not go away — scheduling is still an optimization problem, SSA is
still SSA — but you meet it one well-defined pass at a time, with tools that
show you the IR before and after every step.

---

## 2. What an HLS compiler does, in one page

High-level synthesis (HLS) turns a sequential program into a circuit. A CPU
compiler decides *which instructions* run; an HLS compiler decides *which
hardware exists* and *in which clock cycle* each operation happens.

Take the smallest example in the repository:

```c
// examples/max3.hc
i16 max3(i16 a, i16 b, i16 c) {
  i16 largest = a > b ? a : b;
  return largest > c ? largest : c;
}
```

There are three questions every HLS compiler answers:

1. **What operators are needed?** Two comparators and two multiplexers
   (a `?:` is a multiplexer: it picks one of two wires).
2. **When does each one run?** This is **scheduling**. If a comparator plus a
   multiplexer takes 1.5 ns and the clock period is 6.4 ns, all four operations
   fit in one clock cycle — they are *chained*. If they did not fit, the
   scheduler would split them across two cycles and insert a register between.
3. **What controls the sequence?** A **finite state machine (FSM)** that steps
   through the cycles, plus `start`/`done` signals so the surrounding system
   knows when the result is ready.

For a loop like the dot product in `examples/dot.hc`, there is a fourth
question: **can iterations overlap?** If a new iteration can start every cycle
while earlier ones are still finishing, the loop is *pipelined* with an
*initiation interval* (II) of 1. The accumulator `acc = acc + a[i] * b[i]`
feeds back into itself, and whether II=1 is achievable depends on whether the
adder can finish within one cycle. Working that out is **modulo scheduling**.

Everything in this project serves those four questions.

---

## 3. What already exists in the world

HLS is a mature field. Knowing the landscape lets you talk about the project in
context, and tells us what not to reinvent.

### Commercial tools

| Tool | Vendor | Built on | Input |
| ---- | ------ | -------- | ----- |
| Vitis HLS | AMD (Xilinx) | LLVM (front end open-sourced as `Xilinx/HLS`) | C/C++ |
| HLS Compiler / oneAPI FPGA | Intel (Altera) | LLVM | C++ / SYCL |
| Catapult | Siemens | proprietary | C++ / SystemC |
| SmartHLS | Microchip | LLVM (grew out of LegUp) | C++ |

### Open-source and research tools

| Tool | Where from | Built on | What it is |
| ---- | ---------- | -------- | ---------- |
| Bambu (PandA) | Politecnico di Milano | GCC or Clang | complete C-to-Verilog HLS |
| LegUp | University of Toronto | LLVM | the classic academic HLS; now SmartHLS |
| Dynamatic | EPFL | MLIR | *dynamically* scheduled HLS (handshake circuits, no fixed schedule) |
| ScaleHLS | UIUC | MLIR | optimizes C and emits C++ for Vitis HLS |
| Polygeist | MIT / others | MLIR + Clang | translates C/C++ into MLIR |
| Calyx | Cornell | MLIR (inside CIRCT) | an intermediate language for accelerator generators |
| CIRCT HLS flows | LLVM project | MLIR | experimental static and dynamic HLS pipelines (`hlstool`) |
| XLS | Google | its own IR | HLS from a Rust-like language (DSLX) |

The lesson from this table: **LLVM is the traditional base for HLS tools, and
MLIR is where new work happens.** Using MLIR and CIRCT puts the project in the
same family as Dynamatic, ScaleHLS, and CIRCT's own flows.

---

## 4. The frameworks, explained

### LLVM

A compiler infrastructure: one IR (LLVM IR), hundreds of optimization passes,
and back ends for every CPU and GPU. Its IR is low-level — typed registers,
basic blocks, `phi` nodes — and has no notion of loops-as-structure, clock
cycles, or hardware. We use LLVM indirectly (MLIR is part of the LLVM project,
and we will lower MLIR to LLVM to *run* programs as a test), and there is an
optional LLVM exercise at the end.

### MLIR

"Multi-Level IR": a framework for building IRs. Instead of one fixed IR, MLIR
lets many **dialects** coexist in one program, each at a different level of
abstraction, and provides the machinery to lower from one to the next.

The core concepts, which you will use in every step:

| Concept | Meaning |
| ------- | ------- |
| **Operation** | The one universal building block: `%sum = arith.addi %a, %b : i32`. Everything — an add, a loop, a function, a hardware module — is an operation. |
| **Dialect** | A named group of operations and types: `arith` (math), `scf` (structured loops and ifs), `func` (functions), `memref` (arrays in memory), `hw` (hardware modules). |
| **Region and block** | Operations can contain regions of blocks, which is how a loop contains its body. MLIR uses **block arguments** instead of `phi` nodes — same idea, cleaner form. |
| **Types** | Integers of *any* width are built in: `i17` is a normal MLIR type. That matters for us, because our language has explicit widths. |
| **Pass** | A transformation or analysis over the IR, run by the pass manager. `--canonicalize`, `--cse`, and `--sccp` are passes you get for free. |
| **Rewrite pattern** | A small "if the IR looks like this, replace it with that" rule. Most MLIR optimizations are collections of patterns. |
| **Conversion / lowering** | Rewriting operations of one dialect into another, lower-level one: `arith` into `comb`, `scf` into control flow. |
| **ODS / TableGen** | A declarative language for defining new operations; MLIR generates the C++ from it. |

Tools: `mlir-opt` runs passes on an `.mlir` file and prints the result;
`--mlir-print-ir-after-all` shows the IR after every pass, which is the single
best way to learn what each one does.

### CIRCT

"Circuit IR Compilers and Tools": MLIR dialects and tools for hardware, also
part of the LLVM project. The pieces we use:

| Piece | What it gives us |
| ----- | ---------------- |
| `hw` dialect | Hardware modules, ports, instances: `hw.module @max3(in %a : i16, ...)` |
| `comb` dialect | Combinational logic: `comb.add`, `comb.mux`, `comb.icmp` |
| `seq` dialect | Registers and memories: `seq.compreg`, `seq.firmem` |
| `sv` dialect + ExportVerilog | Turning `hw`/`comb`/`seq` into readable SystemVerilog |
| **Scheduling library** | Ready-made scheduling *problem models* and *solvers* (see below) |
| `pipeline` dialect | A representation of a scheduled, staged pipeline |
| `calyx`, `handshake`, `loopschedule` | Other HLS representations, which we will look at but not depend on |
| `circt-opt`, `firtool` | Command-line drivers, like `mlir-opt` for hardware |

The **scheduling library** is what makes the scheduler tractable. It defines
problem models — `Problem` (operations with latencies and dependencies),
`ChainingProblem` (adds combinational delays, so operations can share a cycle),
`SharedOperatorsProblem` (a limited number of multipliers),
`CyclicProblem` and `ModuloProblem` (loop pipelining with an II) — and solvers
for them: an ASAP worklist scheduler, simplex-based schedulers, and optional
ILP schedulers. A client builds the problem, calls a solver, and reads back
each operation's start time. Our job is to build the *right problem* from our
program and to *use the answer* to generate hardware.

---

## 5. The new pipeline: who provides each stage

```
examples/dot.hc
   │
   │  1. Lexer, parser, AST                      OURS (already built)
   ▼
 AST
   │  2. Semantic analysis: types, width rules,  OURS (written; to be brought back)
   │     constant loop bounds, pragmas
   ▼
 typed AST ────────► AST interpreter             OURS (written; the golden reference)
   │
   │  3. MLIRGen: AST → func/arith/scf/memref    OURS  ← first MLIR code
   ▼
 MLIR (software level)
   │  4. Optimization: canonicalize, CSE, SCCP,  FRAMEWORK (MLIR)
   │     loop unrolling                          + OURS: bit-width narrowing pattern
   │  4b. Check: lower to LLVM and run it,       FRAMEWORK (MLIR → LLVM JIT)
   │      compare with the AST interpreter
   ▼
 optimized MLIR
   │  5. Scheduling: build a ChainingProblem /   OURS, using CIRCT's solvers
   │     ModuloProblem, attach start cycles
   ▼
 scheduled MLIR
   │  6. Hardware generation: FSM controller,    OURS
   │     datapath, registers, memory and
   │     stream ports → hw/comb/seq
   ▼
 CIRCT hardware IR
   │  7. ExportVerilog                           FRAMEWORK (CIRCT)
   ▼
 dot.sv
   │  8. Generated Verilator testbench, checked  OURS (harness) + Verilator
   │     against the AST interpreter
   │  9. Area and timing reports                 Yosys (and Quartus in the lab)
   ▼
 verified hardware + reports
```

---

## 6. What we write ourselves, and why

Each item below is something an interviewer can ask you to explain, and each
exists because no framework does it for us.

**The language frontend (done).** Our input language has explicit bit widths
(`i17`, `u7`) and width-growth rules — addition grows by one bit, multiplication
sums the widths — so no value overflows unless you assign it to something
narrower. That is a *hardware* language decision; C does not work this way. The
lexer, parser, and diagnostics are finished.

**Semantic analysis and the AST interpreter.** Sema applies the width rules,
resolves names, and proves every loop has a compile-time trip count. The
interpreter executes the program exactly as the language defines it. It is the
*golden reference*: every other stage is checked against it.

**MLIRGen (AST → MLIR).** This is the classic "front end emits IR" job, the same
thing Clang does for LLVM and the MLIR Toy tutorial teaches. Two lessons live
here:

- *Mapping source semantics onto IR semantics.* MLIR's `arith` integers are
  *signless*, like our hardware: signedness lives in the operation
  (`arith.extsi` vs `arith.extui`, `arith.divsi` vs `arith.divui`,
  `arith.cmpi slt` vs `ult`). Our width rules become explicit extensions and
  truncations.
- *Where the IR's semantics differ from ours.* In `arith`, dividing by zero and
  shifting by at least the bit width are undefined; in our language they are
  defined. So MLIRGen must emit explicit guards. Knowing that a lowering has to
  close gaps like this is a sign of real compiler understanding.

Loops become `scf.for` with a trip-count counter, and the loop-carried values
(like `acc`) become `iter_args` — MLIR's structured version of phi nodes.

**One optimization pattern: bit-width narrowing.** MLIR's built-in passes handle
constant folding, CSE, and dead code. We add one pattern of our own: the low bits
of a sum depend only on the low bits of its inputs, so
`trunci(addi(extsi a, extsi b))` can become an add at the narrow width. This is
where the dot product's 33-bit intermediate becomes a 32-bit adder. It teaches
rewrite patterns, the most common kind of MLIR code.

**A small dialect for streams.** MLIR has no built-in operation for "read from a
ready/valid stream," so we define `minihls.stream_read` and
`minihls.stream_write` in ODS. Defining a dialect is a standard MLIR skill.

**The scheduling pass.** For each loop body and straight-line region, we build a
CIRCT `ChainingProblem` (or `ModuloProblem` for `#pragma pipeline`), give each
operator a latency and a delay from our delay model, add memory-port limits,
call a solver, and write the resulting cycle onto each operation as an
attribute. We report the achieved II and, when it misses the request, *why*
(the accumulator recurrence, or a memory port). This is the heart of HLS, and
the part you will be asked about most.

**Hardware generation.** From the scheduled IR we build a `hw.module`: a state
register and next-state logic (the FSM), the datapath operations in `comb`,
registers for every value that lives across a cycle boundary, ports for
`start`/`done`/`result`, memory ports for array parameters, and `valid`/`ready`
handshakes for streams. For pipelined loops, per-stage valid bits and a global
stall. This is where "C becomes RTL."

**The verification harness.** For every example the compiler also generates a
self-checking Verilator testbench: random inputs, random stream backpressure,
expected outputs computed by the AST interpreter. Every example must pass on
every change.

### What we deliberately do *not* write

Parsing `.mlir` text, IR data structures, the printer, the verifier, SSA
construction, dominator trees, constant folding, CSE, dead-code elimination,
loop unrolling utilities, the scheduling solvers, and Verilog emission. All of
these come from MLIR and CIRCT, are better tested than anything we would write,
and are what a compiler team would expect you to reuse.

---

## 7. Is this novel?

Honestly: **not as research.** HLS on MLIR already exists — CIRCT's own
`hlstool` flows, Dynamatic, ScaleHLS — and the scheduling algorithms we use are
from published work. Nobody will read this as a new result.

It does not need to be. As a portfolio project it is **distinctive** for four
reasons:

1. **A language designed around hardware widths**, with the width rules carried
   through every stage and verified bit-exactly. Most student HLS projects
   inherit C's `int` and lose this.
2. **Differential verification throughout**: a golden interpreter, MLIR's own
   execution of the IR, and RTL simulation must all agree on every example,
   plus a random program generator later. This is how real compiler teams
   work, and few personal projects do it.
3. **Streams with backpressure and pipelines with stalls** — the hard part of
   real accelerators, and exactly what trading-firm FPGA teams build.
4. **A measured capstone**: an ITCH market-data parser written in the language,
   compiled, and compared against hand-written RTL on latency, area, and clock
   speed, with an explanation of every difference.

If you want a genuinely new angle to talk about, two are within reach: a
measured comparison of CIRCT's schedulers (ASAP vs. simplex vs. ILP) on the
same programs, and running CIRCT's built-in static HLS flow on our examples as
a baseline to compare our generated hardware against.

---

## 8. Step-by-step build plan

Each step is small enough to understand completely before moving on. The
task-level checklist for every step, with effort estimates, is in
[MILESTONES.md](MILESTONES.md). The rule
for the whole project: **do not continue past a step you cannot explain.**

Size: S = a session or two, M = a few sessions, L = a week or more of sessions.

### Step 0 — Toolchain (S)

*Goal:* MLIR and CIRCT installed in WSL, and a "hello world" that goes all the
way to Verilator.

1. Try the prebuilt CIRCT release first (`circt-full-shared-linux-x64.tar.gz`
   from the CIRCT GitHub releases). Check that it contains the MLIR and CIRCT
   headers and CMake package files; if it does, it saves a multi-hour build.
2. If not, build LLVM + MLIR + CIRCT from source in WSL (CIRCT pins its LLVM
   version as a git submodule). Expect 1–2 hours on 8 cores, tens of GB of disk,
   and use `lld` and limited link parallelism so WSL does not run out of memory.
3. Hand-write `max3` as a `hw.module` in `comb`, run `circt-opt
   --export-verilog`, and simulate the output in Verilator.
4. Add a minimal out-of-tree CMake target that links against MLIR, so our code
   can use it.

*You learn:* what the tools are and how an out-of-tree MLIR project is built.
*Done when:* `max3.mlir` → Verilog → Verilator passes, and our build links MLIR.

### Step 1 — Read and run MLIR by hand (S)

*Goal:* be comfortable reading MLIR before generating it.

Write `max3` and `dot` in `func`/`arith`/`scf`/`memref` by hand. Run
`mlir-opt --canonicalize --cse --mlir-print-ir-after-all` and read what
changes. Lower `dot` to the LLVM dialect and run it with MLIR's JIT runner
(`mlir-runner`).

*You learn:* operations, dialects, regions, block arguments vs. phi, passes.
*Interview prep:* "What is MLIR and how does it differ from LLVM IR?"

### Step 2 — Semantic analysis and the golden interpreter (M)

*Goal:* the width rules and the reference model on `main`, reviewed together.

Bring `src/sema/` and the AST interpreter back from the `from-scratch` branch
(see section 9), go through them piece by piece, and add unit tests: the
width-rule table, error messages, and hand-computed expected outputs for every
example.

*You learn:* type checking, scopes, constant evaluation.
*Done when:* every example produces its expected output and bad programs get
clear errors.

### Step 3 — MLIRGen (M)

*Goal:* `minihls emit-mlir examples/dot.hc` prints valid MLIR.

1. Straight-line code first (`max3`, `abs_diff`): expressions become `arith`
   ops, with explicit `extsi`/`extui`/`trunci` for the width rules and guards
   for division and shifts.
2. `if`/`else` becomes `scf.if` with results.
3. Loops become `scf.for` over the trip count with `iter_args` for every
   variable the loop modifies.
4. Arrays become `memref` arguments; `const` arrays become constant globals.

*Check:* lower the generated MLIR to LLVM, run it, and compare with the AST
interpreter on random inputs, for every example. This catches MLIRGen bugs
before any hardware exists.

*You learn:* the MLIR C++ API (`OpBuilder`, locations, types), and how front
ends map language semantics onto an IR.
*Interview prep:* "How does a front end generate SSA?" "What did you have to
be careful about when lowering?"

### Step 4 — Optimization (S–M)

*Goal:* a pass pipeline that cleans up the IR, plus one pattern of our own.

1. Run `canonicalize`, `cse`, and `sccp`; unroll loops marked
   `#pragma unroll` with MLIR's loop-unrolling utility.
2. Write the bit-width narrowing rewrite pattern and its tests.
3. Report operator counts and total datapath bits before and after.

*You learn:* rewrite patterns, the pass manager, `FileCheck`-style IR tests.
*Interview prep:* "Walk me through a pass you wrote."

### Step 5 — The stream dialect (S)

*Goal:* `minihls.stream_read` / `minihls.stream_write` defined in ODS and
emitted by MLIRGen for `stream<T>` parameters.

*You learn:* TableGen/ODS, custom operations and types, verifiers.

### Step 6 — Scheduling (L)

*Goal:* every operation gets a start cycle, within the clock period.

1. A delay model: nanoseconds per operator and width (adders scale with width,
   multipliers use DSP blocks, shifts by constants are free). Calibrated with
   Yosys where possible.
2. For each region, build a CIRCT `ChainingProblem`: operations, dependencies,
   latencies (a memory read takes one cycle), and delays.
3. Add memory-port limits with `SharedOperatorsProblem` (one read port per
   memory).
4. Call a solver, check the solution, and attach `start_cycle` attributes.
5. Print a schedule report per function.

*You learn:* scheduling as constraint solving, operator chaining, resource
constraints, how to use a library's API well.
*Interview prep:* "How does HLS scheduling work? ASAP vs. list vs. SDC?"

### Step 7 — First hardware (L)

*Goal:* Verilog from C for every non-pipelined example, passing co-simulation.

1. Controller: one state per scheduled cycle, `start`/`done`/`idle`, next-state
   logic for loops and branches.
2. Datapath: each operation becomes `comb` logic; a value used in a later cycle
   gets a register (`seq.compreg`) written in the cycle it is produced.
3. Interfaces: scalar parameters as input ports (latched on `start`), the
   return value as an output, memory ports for arrays, ROMs as constant arrays.
4. ExportVerilog, then the generated Verilator testbench against the AST
   interpreter with random inputs.
5. Yosys area report per example.

*You learn:* FSM + datapath design, register insertion, how HLS output maps to
RTL you could write by hand.
*Done when:* every example's RTL passes co-simulation and synthesizes.
*Interview prep:* "What does the RTL your compiler generates look like, and why?"

### Step 8 — Pipelining and streams (L)

*Goal:* `dot.hc` and `stream_sum.hc` pipelined at their best achievable II.

1. Build a `ModuloProblem` for loops marked `#pragma pipeline`, with the
   loop-carried dependence through `acc` as a distance-1 edge.
2. Compute the minimum II from recurrences and memory ports; report requested
   vs. achieved and the limiting reason.
3. Generate a staged datapath with per-stage valid bits and a global stall;
   streams become `valid`/`ready`/`data` ports.
4. Co-simulate under random backpressure.

*You learn:* modulo scheduling, initiation interval, recurrence-constrained vs.
resource-constrained loops, handshake protocols.
*Interview prep:* "What limits the II of a loop?"

### Step 9 — Resource sharing and trade-offs (M)

*Goal:* a latency-versus-area curve: an 8-tap FIR with 1, 2, and 4 multipliers.

Limit multipliers with `SharedOperatorsProblem`, bind operations to shared
units with input multiplexers, and plot latency against Yosys area. Every point
must pass co-simulation.

### Step 10 — Verification hardening (M)

A random program generator for the language, differential testing across the
AST interpreter, MLIR execution, and RTL simulation, and automatic reduction of
failing programs. Log every bug found with its reduced reproducer.

### Step 11 — Capstone: ITCH parsing (L)

An ITCH Add Order field extractor over a `stream<u8>`, compiled with MiniHLS,
compared with a hand-written RTL parser on cycles of latency, area, registers,
DSPs, and Fmax, with a written explanation of each difference.

### Optional side quests

- **LLVM:** write a small LLVM function pass (for example, counting operations
  and estimating their hardware cost) and run it with `opt`. LLVM questions come
  up in interviews too, and this is the quickest way to have a real answer.
- **Baseline:** run CIRCT's built-in static HLS flow on the same examples, if it
  works in the installed release, and compare its output with ours.
- **Scheduler study:** compare CIRCT's ASAP, simplex, and ILP schedulers on
  every example.

---

## 9. What happens to the existing repository

**No need to wipe the repository.** Everything committed on `main` is still
useful:

| Kept on `main` | Why |
| -------------- | --- |
| `src/frontend/` (lexer, parser, AST, diagnostics) and its tests | The front end is independent of what comes after it |
| `src/support/bits.*` | The fixed-width integer rules, used by the interpreter and testbenches |
| `LANGUAGE.md`, `examples/` | The language is unchanged |
| `tests/cosim/`, `hw/`, `scripts/`, `build.ps1` | The Verilator co-simulation pattern and build scripts carry over |
| `docs/GUIDE.md`, `docs/COMPILERS_AND_SYSTEMS.md` | Background on the hardware tools, still accurate |

**The half-finished from-scratch middle end** — semantic analysis, both
interpreters, the hand-written SSA IR, and the optimization passes — was never
committed to `main`. It is saved on the **`from-scratch` branch** and nothing
from it is on `main` now.

In Step 2 we bring back only what the new design still needs:

- `src/sema/` — semantic analysis (needed: MLIRGen reads its types)
- `src/interp/ast_interp.*` and `src/interp/io.*` — the golden reference
- `src/support/int128.hpp` — exact arithmetic for the interpreter
- the annotation fields added to `src/frontend/ast.hpp`

The hand-written IR (`src/ir/`), the IR interpreter, and the passes
(`src/passes/`) stay on the branch as reference: MLIR replaces them. Reading
them next to their MLIR equivalents is a good way to see what the framework is
doing for you.

---

## 10. Reading list

In rough order of usefulness:

1. **MLIR Toy tutorial** (mlir.llvm.org, "Toy Tutorial") — builds a language
   front end on MLIR in seven chapters. Steps 3–5 follow the same path.
2. **CIRCT documentation** (circt.llvm.org) — the `hw`, `comb`, and `seq`
   dialects, "Static scheduling infrastructure," and "HLS in CIRCT."
3. Kastner, Matai, and Neuendorffer, *Parallel Programming for FPGAs* (free
   online) — what HLS tools do and why, from the user's side.
4. Lattner et al., "MLIR: Scaling Compiler Infrastructure for Domain Specific
   Computation," CGO 2021 — why MLIR is designed the way it is.
5. Cong and Zhang, "An Efficient and Versatile Scheduling Algorithm Based on SDC
   Formulation," DAC 2006 — the scheduling formulation behind the simplex
   schedulers.
6. Canis et al., "LegUp," FPGA 2011 — how a complete LLVM-based HLS tool is
   organized.

---

## 11. Glossary

| Term | Meaning |
| ---- | ------- |
| **HLS** | High-level synthesis: compiling a software-like description into hardware. |
| **RTL** | Register-transfer level: hardware described as registers and the logic between them; Verilog/SystemVerilog is RTL. |
| **IR** | Intermediate representation: the compiler's internal form of the program. |
| **SSA** | Static single assignment: every value is assigned exactly once, which makes dataflow explicit. |
| **Phi / block argument** | Where control-flow paths join, the value that depends on which path was taken. In hardware, a multiplexer on a register's input. |
| **Dialect** | In MLIR, a named set of operations and types for one level of abstraction. |
| **Lowering** | Rewriting IR into a lower-level form, such as `arith` into `comb`. |
| **Scheduling** | Assigning each operation to a clock cycle. |
| **Chaining** | Putting dependent operations in the same cycle when their delays fit in the clock period. |
| **II** | Initiation interval: cycles between starts of consecutive loop iterations in a pipeline. |
| **Modulo scheduling** | Scheduling a loop body so iterations overlap at a fixed II. |
| **Binding** | Assigning operations to physical functional units, possibly shared. |
| **FSM** | Finite state machine: the controller that steps through the schedule. |
| **Co-simulation** | Simulating the generated RTL and comparing it with a software model. |
| **Fmax** | The highest clock frequency the synthesized circuit meets timing at. |
