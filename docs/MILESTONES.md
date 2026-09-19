# MiniHLS milestone plan: what is done and what is left

This is the task-level plan. [PLAN.md](PLAN.md) explains *why* the project is
shaped this way and what each step teaches; this file is the checklist: what
to build, where it goes, how it is tested, what number to record, and how long
it should take.

- [Where things stand](#where-things-stand)
- [The critical path](#the-critical-path)
- [Milestones](#milestones)
- [Effort summary](#effort-summary)
- [Risks and fallbacks](#risks-and-fallbacks)
- [Results to record](#results-to-record)

---

## Where things stand

| Area | Status |
| ---- | ------ |
| Build system, GoogleTest, Verilator co-simulation harness, Yosys area script | **Done** (milestone 0) |
| Lexer, Pratt parser, AST, printer, diagnostics, `LANGUAGE.md`, six examples, 60 tests | **Done** (milestone 1) |
| Semantic analysis and AST interpreter | **Done** (P2): `minihls check` and `minihls run`, 58 new tests |
| MLIR/CIRCT toolchain | In progress (P0): building from source, since this machine is ARM64 |
| Hand-written SSA IR, IR interpreter, optimization passes | On `from-scratch`, replaced by MLIR; reference only |
| Everything from MLIR onward | Not started |

Roughly **15–20% of the total work is done**. The front end is complete and
solid; the parts that make this an HLS compiler are all ahead.

---

## The critical path

```
P0 toolchain ─► P1 read MLIR ─► P2 sema + interpreter ─► P3 MLIRGen ─► P4 optimization
                                                                            │
                                     P5 stream dialect ◄────────────────────┤
                                            │                               ▼
                                            └──────────────────────► P6 scheduling
                                                                            │
                                                                            ▼
                                                                    P7 first hardware   ◄── minimum complete project
                                                                            │
                                                     ┌──────────────────────┼────────────────────┐
                                                     ▼                      ▼                    ▼
                                             P8 pipelining          P9 resource sharing   P10 fuzzing
                                                     │
                                                     ▼
                                             P11 ITCH capstone
```

**P7 is the minimum complete project**: C-like source in, verified Verilog out.
**P8 is where it becomes impressive**: pipelined loops and backpressure are what
real accelerators need. P9–P11 make it distinctive.

---

## Milestones

Each milestone has: what to build, where it lives, how it is tested, what
"done" means, and the number to write down when it is done (these become the
figures in your resume bullets).

### P0 — Toolchain

**Goal:** MLIR and CIRCT usable from this repository's CMake build, and a
hand-written hardware module simulated end to end.

Tasks:
- [ ] Download the CIRCT prebuilt release (`circt-full-shared-linux-x64.tar.gz`)
      into WSL and check for `lib/cmake/mlir/MLIRConfig.cmake`,
      `lib/cmake/circt/CIRCTConfig.cmake`, headers, and the `circt-opt` and
      `mlir-opt` binaries.
- [ ] If anything is missing, build LLVM + MLIR + CIRCT from source at the
      LLVM commit CIRCT pins (`-DLLVM_ENABLE_PROJECTS=mlir`,
      `-DLLVM_TARGETS_TO_BUILD=host`, `-DLLVM_USE_LINKER=lld`,
      `-DLLVM_PARALLEL_LINK_JOBS=2`), installed under `~/.local/circt`.
- [ ] `hw/max3.mlir`: `max3` hand-written in `hw` + `comb`.
- [ ] `circt-opt hw/max3.mlir --export-verilog` → `max3.sv`, simulated with a
      small Verilator test.
- [ ] CMake: `find_package(MLIR)` and `find_package(CIRCT)` behind a
      `MINIHLS_MLIR` option; a trivial target that creates an `MLIRContext`
      proves linking works.
- [ ] Update `scripts/build_and_test.sh` and the README requirements.

Tests: the Verilator test for `max3.sv` joins the `cosim` suite.
Done when: one command builds the project against MLIR and runs all suites.
Effort: 4–10 hours (the high end if building from source).

### P1 — Read and run MLIR by hand

**Goal:** fluency reading the IR before generating it. No project code.

Tasks:
- [ ] `docs/mlir-by-hand/max3.mlir` and `dot.mlir` in `func`/`arith`/`scf`/`memref`.
- [ ] Run `mlir-opt --canonicalize --cse --mlir-print-ir-after-all`; write two
      or three sentences on what each pass changed.
- [ ] Lower `dot.mlir` to the LLVM dialect and run it with `mlir-runner`;
      check the result against a hand calculation.
- [ ] Work through chapters 1–3 of the MLIR Toy tutorial.

Done when: you can explain every line of both files, including `iter_args`.
Effort: 4–8 hours.

### P2 — Semantic analysis and the golden interpreter

**Goal:** the width rules and the reference model on `main`, reviewed and tested.

Tasks:
- [x] Bring back from `from-scratch`: `src/sema/`, `src/interp/ast_interp.*`,
      `src/interp/io.*`, `src/support/int128.hpp`, and the annotation fields in
      `src/frontend/ast.hpp`.
- [ ] Review them file by file (your task); ask about or simplify anything you
      cannot explain.
- [x] `minihls check file.hc` and `minihls run file.hc --top f --arg a=3 ...`
      in the driver.
- [x] Unit tests: every row of the width table in `LANGUAGE.md`; every sema
      error (undeclared name, shadowing, non-constant loop bound, `read()` inside
      `&&`, width over 64, return inside a loop); hand-computed expected outputs
      for all six examples.

Done when: all examples produce their expected outputs and every invalid
program is rejected with a clear message.
Record: number of unit tests.
**Status: done.** 118 unit tests. Runtime behaviour (division by zero, shift
amounts, uninitialized values) is now specified in `LANGUAGE.md`.
Effort: 8–14 hours.

### P3 — MLIRGen

**Goal:** `minihls emit-mlir examples/dot.hc --top dot` prints valid MLIR, and
running that MLIR gives the same answers as the interpreter.

Tasks:
- [ ] `src/mlirgen/`: an `MLIRGen` class walking the typed AST with an
      `OpBuilder`; source locations attached to every operation.
- [ ] Expressions: `arith` ops with explicit `extsi`/`extui`/`trunci` for the
      width rules; comparisons with the right signed/unsigned predicate.
- [ ] Guards where MLIR's semantics differ from ours: division and remainder by
      zero, and shifts by at least the width.
- [ ] `if`/`else` → `scf.if` yielding every variable either branch assigns.
- [ ] `for` → `scf.for` over the trip count, with the induction variable and
      every modified variable as `iter_args`.
- [ ] Arrays → `memref` arguments; `const` arrays → `memref.global` constants.
- [ ] Execution check: lower to LLVM, JIT with MLIR's `ExecutionEngine`, and
      compare against the AST interpreter on 1,000 random inputs per example.
- [ ] FileCheck tests for the shape of the generated IR.

Done when: every example passes the execution check.
Record: number of examples and random vectors checked.
Effort: 15–25 hours.

### P4 — Optimization

**Goal:** a pass pipeline, one pattern of our own, and a before/after report.

Tasks:
- [ ] `src/transforms/pipeline.cpp`: `canonicalize`, `cse`, `sccp`, and full or
      partial unrolling of loops marked `#pragma unroll`.
- [ ] `NarrowArithmetic` rewrite pattern: `trunci(op(ext a, ext b))` → `op` at
      the narrow width, for add, sub, mul, and, or, xor.
- [ ] Execution check after every pass (reuse P3's harness).
- [ ] `--report ops`: operator counts and total datapath bits before and after.

Done when: every example still passes the execution check after every pass.
Record: operator count and bit reduction per example (e.g. dot's 33-bit add →
32-bit).
Effort: 8–14 hours.

### P5 — Stream dialect

**Goal:** `stream<T>` parameters represented in MLIR.

Tasks:
- [ ] `src/dialect/MiniHLSOps.td`: a `!minihls.stream<i8>` type and
      `minihls.stream_read` / `minihls.stream_write` ops, defined in ODS.
- [ ] Verifiers (element type matches, direction consistent).
- [ ] MLIRGen emits them; the execution check models streams as buffers.

Done when: `stream_sum.hc` passes the execution check.
Effort: 5–10 hours.

### P6 — Scheduling

**Goal:** every operation has a start cycle, and the schedule respects the
clock period, dependencies, and memory ports.

Tasks:
- [ ] `src/hls/delay_model.cpp`: delay and latency per operator and width
      (adders scale with width, multipliers go to DSPs, constant shifts and
      extensions are free, memory reads take one cycle).
- [ ] Calibrate the delays with Yosys where possible; document the rest.
- [ ] `src/hls/schedule.cpp`: for each straight-line region and loop body, build
      a CIRCT `ChainingProblem`; add one-port-per-memory limits via
      `SharedOperatorsProblem`; solve with the simplex scheduler; verify the
      solution.
- [ ] Attach a `minihls.start_cycle` attribute to every operation.
- [ ] `--report schedule`: cycles per region, total latency, critical path.

Done when: every example schedules and the report matches a hand calculation
for `max3` (one cycle) and `popcount` (chained).
Record: latency in cycles per example at 6.4 ns.
Effort: 15–25 hours.

### P7 — First hardware (minimum complete project)

**Goal:** Verilog from source for every example, passing co-simulation.

Tasks:
- [ ] `src/hls/hwgen.cpp`: build a `hw.module` per top function.
- [ ] Controller: one state per scheduled cycle, `start`/`done`/`idle`,
      next-state logic for loops and branches.
- [ ] Datapath: each operation as `comb` logic; a `seq.compreg` for every value
      used in a later cycle than it is produced.
- [ ] Interfaces: scalar inputs latched on `start`, `result` output, a read and
      a write port per array parameter, ROMs as constant arrays.
- [ ] ExportVerilog → `minihls compile file.hc --top f --emit-sv f.sv`.
- [ ] `minihls testbench`: generate a self-checking Verilator testbench with
      random inputs and expected outputs from the AST interpreter.
- [ ] `scripts/cosim.sh file.hc top`: compile, generate the testbench, build
      with Verilator, run. One CTest entry per example.
- [ ] Yosys synthesis of each generated module: cell counts per example.

Done when: every non-pipelined example passes co-simulation on at least 1,000
random vectors and synthesizes in Yosys.
Record: examples passing, cycles and Yosys cells per example.
Effort: 25–40 hours.

### P8 — Pipelining and streams

**Goal:** `dot.hc` at II=1 and `stream_sum.hc` at its best II, both correct
under random backpressure.

Tasks:
- [ ] `ModuloProblem` for loops marked `#pragma pipeline`, with loop-carried
      dependences as distance-1 edges.
- [ ] Minimum II from recurrences and memory ports; report requested vs.
      achieved II and the limiting reason.
- [ ] Pipelined datapath: stage registers, per-stage valid bits, an iteration
      counter, a global stall.
- [ ] Streams as `data`/`valid`/`ready` ports; stall when a stream is not ready.
- [ ] Testbench: random `valid` gaps on inputs and random `ready` on outputs.

Done when: both examples pass co-simulation under random backpressure at their
reported II.
Record: achieved II, total cycles (for dot, about 64 + pipeline depth rather
than ~128–192 unpipelined), Fmax estimate.
Effort: 25–40 hours.

### P9 — Resource sharing and trade-offs

**Goal:** a measured latency-versus-area curve.

Tasks:
- [ ] `--max-mul N`: limit multipliers with `SharedOperatorsProblem`.
- [ ] Binding: map operations onto shared units with input multiplexers.
- [ ] `scripts/tradeoff.sh`: sweep N = 1, 2, 4, 8 on the FIR example, run
      co-simulation for each point, and record cycles and Yosys cells.
- [ ] A plot in `bench/`.

Done when: every point on the curve passes co-simulation.
Record: the curve (cycles vs. cells).
Effort: 10–15 hours.

### P10 — Verification hardening

**Goal:** evidence that the compiler is correct beyond six examples.

Tasks:
- [ ] A random program generator for the language (well-typed, in-bounds,
      terminating by construction).
- [ ] Differential testing: AST interpreter vs. MLIR execution after every pass
      vs. RTL simulation.
- [ ] A delta-debugging reducer for failing programs.
- [ ] Runs under AddressSanitizer and UndefinedBehaviorSanitizer.
- [ ] `bench/bugs.md`: every bug found, its reduced reproducer, and the fix.

Done when: thousands of random programs pass with no open bugs.
Record: programs tested, bugs found and fixed.
Effort: 15–20 hours.

### P11 — Capstone: ITCH parsing

**Goal:** a head-to-head comparison against hand-written RTL.

Tasks:
- [ ] `examples/itch_add_order.hc`: extract order reference, side, shares,
      stock, and price from an ITCH Add Order message on a `stream<u8>`.
- [ ] `hw/itch_add_order.sv`: the same parser written by hand.
- [ ] Co-simulate both against the same message traces.
- [ ] Compare latency (cycles), area (Yosys cells; ALMs and registers in
      Quartus if available), and Fmax.
- [ ] `bench/itch.md`: the numbers and an explanation of every difference.

Done when: the write-up exists with measured numbers.
Record: every number in the comparison.
Effort: 20–30 hours.

### Optional

- **LLVM side quest (4–6 h):** an LLVM function pass that counts operations and
  estimates hardware cost, run with `opt`.
- **CIRCT baseline (4–8 h):** CIRCT's own static HLS flow on the same examples,
  compared with ours.
- **Scheduler study (4–6 h):** ASAP vs. simplex vs. ILP on every example.

---

## Effort summary

| Milestone | Hours |
| --------- | ----- |
| P0 Toolchain | 4–10 |
| P1 Read MLIR | 4–8 |
| P2 Sema + interpreter | 8–14 |
| P3 MLIRGen | 15–25 |
| P4 Optimization | 8–14 |
| P5 Stream dialect | 5–10 |
| P6 Scheduling | 15–25 |
| P7 First hardware | 25–40 |
| **Through P7 (minimum complete)** | **~85–145** |
| P8 Pipelining and streams | 25–40 |
| P9 Resource sharing | 10–15 |
| P10 Verification hardening | 15–20 |
| P11 ITCH capstone | 20–30 |
| **Everything** | **~155–250** |

At about 10 hours a week, P7 is 2–3 months away and the full project 4–6
months. These are estimates for someone learning MLIR as they go; the first
two MLIR milestones are the slowest.

---

## Risks and fallbacks

| Risk | Fallback |
| ---- | -------- |
| The prebuilt CIRCT release lacks development files | Source build (budgeted in P0) |
| WSL runs out of memory linking LLVM | `LLVM_PARALLEL_LINK_JOBS=1`, raise the WSL memory limit in `.wslconfig` |
| A CIRCT API changes between releases | Pin the release version in the build script and README |
| The scheduling library does not cover a case we need | Write that case by hand (the `from-scratch` branch has a list scheduler design to borrow) |
| Pipelined hardware generation is harder than planned | Ship P7 first; it is a complete project on its own |
| Quartus is not available | Report Yosys cells and a delay-model Fmax estimate, and say so |

---

## Results to record

Fill this table in as milestones complete; it is the source of every number on
your resume.

| Metric | Value | From |
| ------ | ----- | ---- |
| Examples passing co-simulation | | P7 |
| Random vectors per example | | P7 |
| dot: achieved II / total cycles | | P8 |
| stream_sum: achieved II under backpressure | | P8 |
| Datapath bits removed by narrowing | | P4 |
| FIR latency-vs-area points | | P9 |
| Random programs tested / bugs found | | P10 |
| ITCH: cycles, cells, Fmax (MiniHLS vs. hand-written) | | P11 |
| Unit + MLIR + cosim test count | | all |
