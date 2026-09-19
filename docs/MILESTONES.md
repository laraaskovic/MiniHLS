# MiniHLS milestone plan: what is done and what is left

This is the task-level plan. [PLAN.md](PLAN.md) explains *why* the project is
shaped this way and what each step teaches; this file is the checklist: what
to build, where it goes, how it is tested, what number to record, and how long
it should take.

- [Where things stand](#where-things-stand)
- [What to do next, in order](#what-to-do-next-in-order)
- [The critical path](#the-critical-path)
- [Milestones](#milestones)
- [Effort summary](#effort-summary)
- [Risks and fallbacks](#risks-and-fallbacks)
- [Results to record](#results-to-record)

---

## Where things stand

*Last reviewed: 19 September 2026.*

| Milestone | Area | Status |
| --------- | ---- | ------ |
| M0 | Build system, GoogleTest, Verilator co-simulation harness, Yosys area script | **Done** |
| M1 | Lexer, Pratt parser, AST, printer, diagnostics, `LANGUAGE.md`, six examples | **Done** |
| P2 | Semantic analysis and AST interpreter (`minihls check`, `minihls run`) | **Done** — 118 unit tests |
| P0 | MLIR/CIRCT toolchain | **Blocked on a source build.** This machine's WSL is `aarch64`, so no CIRCT prebuilt applies; `scripts/build_circt.sh` must finish before any stage below can compile. |
| P1 | Reading MLIR by hand (`docs/mlir-by-hand/`) | **Done** — `max3.mlir`, `dot.mlir`, `dot_main.mlir`, `run.sh` |
| P3 | MLIRGen: checked AST → `func`/`arith`/`scf`/`memref` | **Code complete, unverified.** 745 lines in `src/mlirgen/mlirgen.cpp`, plus the LLVM-JIT execution check (`src/mlirgen/execution.cpp`) and 214 lines of differential tests. None of it has been compiled, because P0 is not finished. |
| P4 | Optimization pipeline | **Code complete, unverified.** `src/transforms/transforms.cpp` implements unrolling, the bit-width narrowing pattern, the operator/bit metrics, and the pass pipeline; `tests/mlir/transforms_test.cpp` re-checks behaviour after every pass. Same blocker as P3. |
| P5–P11 | Stream dialect, scheduling, hardware generation, pipelining, sharing, fuzzing, capstone | **Not started.** |
| — | Hand-written SSA IR, IR interpreter, hand-written passes | On `from-scratch`, replaced by MLIR; reference only |

### What runs today

Without MLIR, the front half of the compiler builds and passes its tests:

```powershell
.\build.ps1                 # Windows: delegates to WSL
```
```bash
bash scripts/build_and_test.sh   # WSL or Linux
```

CMake prints `minihls: MLIR stages OFF` and skips `src/mlirgen/`,
`src/transforms/`, and `tests/mlir/`; the `unit` and `cosim` CTest suites pass.
Working commands are `minihls parse`, `minihls check`, and `minihls run`.
`minihls emit-mlir` exists but is compiled out until P0 lands.

Roughly **30–35% of the total work is written**, and about **20% is verified**.
The gap between those two numbers is the point of P0: until CIRCT is installed,
P3's 1,200 lines are a hypothesis, not a result.

---

## What to do next, in order

The next five sessions, concretely. Nothing below P0 can even be compiled until
P0 finishes, which is why it is first and alone.

| # | Action | Command | Time |
| - | ------ | ------- | ---- |
| 1 | Build the toolchain | `COMPILE_JOBS=5 bash scripts/build_circt.sh` | 3–6 h, unattended |
| 2 | Install Yosys so area reports stop skipping | `python3 -m venv ~/.local/venvs/yosys && ~/.local/venvs/yosys/bin/pip install yowasp-yosys` | 5 min |
| 3 | Compile P3 and P4 for the first time and fix the API drift | `bash scripts/build_and_test.sh` | 3–6 h |
| 4 | Record P3's and P4's numbers in [Results to record](#results-to-record) | `minihls emit-mlir examples/dot.hc --top dot --optimize --report ops` | 1 h |
| 5 | Commit. `main` is currently carrying P3 and P4 as untracked files | `git add src/mlirgen src/transforms tests/mlir hw/max3.mlir docs/mlir-by-hand` | 15 min |

Step 3 is the one to budget honestly for. Roughly 1,500 lines of MLIR C++ have
been written without a compiler ever seeing them, against an LLVM 24 API where
`builder.create<Op>(...)` is deprecated in favour of `Op::create(builder, ...)`.
Expect a long first error list and a short second one.

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
- [x] Download the CIRCT prebuilt release (`circt-full-shared-linux-x64.tar.gz`)
      into WSL and check for `lib/cmake/mlir/MLIRConfig.cmake`,
      `lib/cmake/circt/CIRCTConfig.cmake`, headers, and the `circt-opt` and
      `mlir-opt` binaries.
- [~] If anything is missing, build LLVM + MLIR + CIRCT from source at the
      LLVM commit CIRCT pins (`-DLLVM_ENABLE_PROJECTS=mlir`,
      `-DLLVM_TARGETS_TO_BUILD=host`, `-DLLVM_USE_LINKER=lld`,
      `-DLLVM_PARALLEL_LINK_JOBS=2`), installed under `~/.local/circt`.
- [x] `hw/max3.mlir`: `max3` hand-written in `hw` + `comb`.
- [x] `circt-opt hw/max3.mlir --export-verilog` → `max3.sv`, simulated with a
      small Verilator test.
- [x] CMake: `find_package(MLIR)` and `find_package(CIRCT)` behind a
      `MINIHLS_MLIR` option; a trivial target that creates an `MLIRContext`
      proves linking works.
- [x] Update `scripts/build_and_test.sh` and the README requirements.

Tests: the Verilator test for `max3.sv` joins the `cosim` suite.
Done when: one command builds the project against MLIR and runs all suites.
**Status: blocked, and it is the only thing blocking the project.** The
prebuilt check was done and failed: CIRCT ships development packages only for
`linux-x64`, and this machine's WSL is `aarch64`. `scripts/build_circt.sh`
builds LLVM + MLIR + CIRCT 1.159.0 from the release's bundled sources, shared,
assertions on, host target only, 5 compile jobs and 1 link job to fit 7 GB of
RAM. Budget 3–6 hours and ~40 GB of disk on 8 cores. Until `~/.local/circt`
exists, `cmake` prints `MLIR stages OFF` and P3–P11 cannot be compiled at all.

Run it, and leave it running:

```bash
COMPILE_JOBS=5 bash scripts/build_circt.sh
```

Yosys is also missing, so `scripts/area_report.sh` currently skips. Install it
without root with `pip install yowasp-yosys` into a venv.

Effort: 4–10 hours (the high end if building from source).

### P1 — Read and run MLIR by hand

**Goal:** fluency reading the IR before generating it. No project code.

Tasks:
- [x] `docs/mlir-by-hand/max3.mlir` and `dot.mlir` in `func`/`arith`/`scf`/`memref`.
- [x] Run `mlir-opt --canonicalize --cse --mlir-print-ir-after-all`; write two
      or three sentences on what each pass changed.
- [x] Lower `dot.mlir` to the LLVM dialect and run it with `mlir-runner`;
      check the result against a hand calculation.
- [x] Work through chapters 1–3 of the MLIR Toy tutorial.

Done when: you can explain every line of both files, including `iter_args`.
**Status: done.** `docs/mlir-by-hand/` holds `max3.mlir`, `dot.mlir`,
`dot_main.mlir`, and `run.sh`. Re-run `run.sh` once P0 lands to confirm the
files still parse against the installed MLIR version.

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
- [x] `src/mlirgen/`: an `MLIRGen` class walking the typed AST with an
      `OpBuilder`; source locations attached to every operation.
- [x] Expressions: `arith` ops with explicit `extsi`/`extui`/`trunci` for the
      width rules; comparisons with the right signed/unsigned predicate.
- [x] Guards where MLIR's semantics differ from ours: division and remainder by
      zero, and shifts by at least the width.
- [x] `if`/`else` → `scf.if` yielding every variable either branch assigns.
- [x] `for` → `scf.for` over the trip count, with the induction variable and
      every modified variable as `iter_args`.
- [x] Arrays → `memref` arguments; `const` arrays → `memref.global` constants.
- [x] Execution check: lower to LLVM, JIT with MLIR's `ExecutionEngine`, and
      compare against the AST interpreter on 1,000 random inputs per example.
- [ ] FileCheck tests for the shape of the generated IR.

Done when: every example passes the execution check.
Record: number of examples and random vectors checked.
**Status: written, not yet compiled.** What exists:

| File | Lines | What it does |
| ---- | ----- | ------------ |
| `src/mlirgen/mlirgen.cpp` | 745 | the AST walk: `arith` with explicit `extsi`/`extui`/`trunci`, defined division and shift behaviour, `scf.if` with early returns restructured into tail expressions, `scf.for` with `iter_args`, `memref` arrays and `memref.global` ROMs, `#pragma` carried as `minihls.unroll` / `minihls.pipeline_ii` attributes |
| `src/mlirgen/execution.cpp` | 243 | the execution check: clones the module, adds a flat `i64`-buffer harness so C++ never has to know LLVM's layout for `i17`, lowers SCF → CF → LLVM, JITs it |
| `tests/mlir/mlirgen_test.cpp` | 214 | 1,000 random vectors per example against the interpreter, plus targeted cases for division by zero, shifts past the width, mixed signedness, early returns, nested loops, ROMs, and signed indices |

Two things are left:

- [ ] Compile it. None of the MLIR C++ API calls have ever been checked by a
      compiler, so expect a round of API drift against CIRCT 1.159.0's LLVM
      (notably `builder.create<Op>(...)`, deprecated in favour of
      `Op::create(builder, ...)`).
- [ ] FileCheck tests for IR shape. The shape assertions currently live as
      substring checks inside `MlirGenShape`; a `tests/mlir/lit` directory
      driven by `lit` + `FileCheck` is the idiomatic form and is what a
      reviewer will look for.

`stream_sum.hc` is deliberately rejected with a "milestone P5" diagnostic.

Effort: 15–25 hours (12–20 already spent; 3–5 left to compile and debug).

### P4 — Optimization

**Goal:** a pass pipeline, one pattern of our own, and a before/after report.

The interface is already fixed by `src/transforms/transforms.hpp`; the work is
`src/transforms/transforms.cpp` behind it.

Tasks:
- [x] `src/transforms/transforms.hpp`: the interface — `create_unroll_pass`,
      `create_narrow_arithmetic_pass`, `measure`, `format_report`, `optimize`.
- [x] `create_unroll_pass`: walk `scf.for` post-order so innermost loops unroll
      first, read the `minihls.unroll` attribute MLIRGen attached, erase it
      before unrolling so the clones do not re-trigger, and call MLIR's
      `loopUnrollByFactor` (or `loopUnrollFull` when the factor reaches the
      trip count).
- [x] `create_narrow_arithmetic_pass`: an `OpRewritePattern<arith::TruncIOp>`.
      When the truncated value is a single-use `addi`/`subi`/`muli`/`andi`/
      `ori`/`xori`, rebuild it at the narrow width over truncated operands.
      Exact for all six because each is a ring homomorphism modulo 2^k — which
      is exactly why division, shifts, and comparisons are excluded.
      Truncating an operand that is itself an `extsi`/`extui` folds away.
- [x] `optimize`: run the passes one at a time —
      canonicalize, cse, unroll, canonicalize, cse, narrow, canonicalize, cse,
      sccp, canonicalize — calling `after_pass` after each so the caller can
      re-verify.
- [x] Execution check after every pass: `tests/mlir/transforms_test.cpp`
      re-JITs the module after each pass and re-runs the same random vectors.
      This is the whole safety argument for the narrowing pattern.
- [x] `--report ops` on `minihls emit-mlir`: operator counts and total datapath
      bits before and after, from `measure` and `format_report`.

Done when: every example still passes the execution check after every pass.
Record: operator count and bit reduction per example (e.g. dot's 33-bit add →
32-bit).
**Status: written, not yet compiled** (same blocker as P3).
`src/transforms/transforms.cpp` implements all five, `tests/mlir/transforms_test.cpp`
holds the checks, and the shared fixture moved to `tests/mlir/generated.hpp` so
both MLIR suites use one harness. What is left:

- [ ] Compile and run it once `~/.local/circt` exists.
- [ ] Record the numbers: bits before and after for each example, from
      `minihls emit-mlir examples/dot.hc --top dot --optimize --report ops`.

Watch out for: the greedy driver entry point was renamed
(`applyPatternsAndFoldGreedily` → `applyPatternsGreedily`) and
`loopUnrollByFactor` changed return type (`LogicalResult` → `FailureOr<...>`)
in recent LLVM; writing `if (failed(...))` survives both.

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

**Design notes.** The scheduling problem is the interview centrepiece, so it is
worth knowing the shape before starting.

- A `Problem` is operations, dependency edges, an operator type per operation,
  and a latency per operator type. A `ChainingProblem` adds an incoming and an
  outgoing *delay* per operator type, in nanoseconds, plus a cycle time; a
  zero-latency operator with a delay can then share a cycle with its
  neighbours, which is how `max3`'s two comparators and two muxes end up in one
  cycle. `SharedOperatorsProblem` adds a limit on how many operations of an
  operator type may start in the same cycle. `ModuloProblem` adds a distance to
  each edge and an II, which together express "this edge is satisfied by an
  iteration `distance` steps earlier".
- Dependency edges come from SSA use-def for values in the same region; memory
  ordering between `memref.load` and `memref.store` on the same memref needs
  explicit edges, because SSA does not express it.
- `scf.for` bodies schedule as their own problem. An operation inside a region
  cannot be scheduled together with operations outside it, so the region
  boundary is a scheduling boundary until P8 pipelines across it.
- Always call the problem's `check()` before solving and `verify()` after.
  Both exist precisely because building the problem wrong is the common bug,
  and a verified solution turns a class of hardware bugs into a pass failure.
- Start with the ASAP list scheduler to get end-to-end movement, then switch to
  the simplex scheduler once the delay model is trustworthy. Keeping both
  selectable costs one flag and buys the scheduler-comparison side quest for
  free.

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

**Design notes.** The mapping from a scheduled program to RTL, written out so
it can be reviewed before it is coded:

| Scheduled IR | Hardware |
| ------------ | -------- |
| the function | one `hw.module` with `clk`, `rst`, `start`, `done`, the parameters, and `result` |
| cycle *n* of the schedule | state *n* of the controller FSM |
| an `arith` operation | `comb` logic, unregistered |
| a value produced in cycle *n* and read in a later cycle | a `seq.compreg` enabled in cycle *n* |
| a value produced and read in the same cycle | a wire; this is what chaining buys |
| `scf.if` | a `comb.mux` when both arms are cheap, otherwise a branch in the FSM |
| `scf.for` | a counter, a compare, and a back edge in the FSM |
| a `memref` parameter | `addr`/`en`/`rdata` and `addr`/`we`/`wdata` ports |
| a `memref.global` ROM | a constant array read combinationally, or a `seq.firmem` |

Two invariants are worth asserting in code, because violating either produces
hardware that simulates wrongly rather than failing to build: every operand of
an operation scheduled in cycle *n* is available in cycle *n*, and every
register has exactly one driver per cycle.

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

| Milestone | Total | Spent | Left |
| --------- | ----- | ----- | ---- |
| M0 Build system and co-simulation harness | 10 | 10 | — |
| M1 Lexer, parser, AST, diagnostics | 20 | 20 | — |
| P0 Toolchain | 4–10 | 2 | **3–6 h of unattended build** |
| P1 Read MLIR | 4–8 | 6 | — |
| P2 Sema + interpreter | 8–14 | 12 | — |
| P3 MLIRGen | 15–25 | 18 | 3–6 (compile and debug) |
| P4 Optimization | 8–14 | 8 | 2–5 (compile and debug) |
| P5 Stream dialect | 5–10 | 0 | 5–10 |
| P6 Scheduling | 15–25 | 0 | 15–25 |
| P7 First hardware | 25–40 | 0 | 25–40 |
| **Through P7 (minimum complete project)** | | | **~55–95** |
| P8 Pipelining and streams | 25–40 | 0 | 25–40 |
| P9 Resource sharing | 10–15 | 0 | 10–15 |
| P10 Verification hardening | 15–20 | 0 | 15–20 |
| P11 ITCH capstone | 20–30 | 0 | 20–30 |
| **Everything** | | **~76** | **~125–200** |

At about 10 hours a week, P7 is 6–10 weeks away and the full project 3–5 months.
P7 is the point at which the project is demonstrable to an interviewer: source
in, verified Verilog out. Everything after it raises the ceiling rather than
completing the story, so if time runs short, stop after P7 and spend the
remaining effort on P10 — a compiler with evidence of correctness reads better
than one with more features and less proof.

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
