# MiniHLS roadmap: epics and stories

This is the *delivery* plan. It slices the project into *epics* (a chunk of
the compiler, tracked as one GitHub issue) and *stories* (one concept, one
branch, one pull request).

- [PLAN.md](PLAN.md) explains **why** the project is shaped this way.
- [MILESTONES.md](MILESTONES.md) is the task-level checklist with effort
  estimates.
- **This file** is how the work becomes reviewable pull requests you can
  point an interviewer at.

---

## How to use this

A **story is a pull request.** If a story is too big to review in one sitting,
it is two stories. If two stories can't be explained apart from each other,
they are one story.

The rule that matters more than any of the others:

> **Do not merge a story you cannot explain out loud.**

The point of this project is not a finished compiler. It is being able to
answer, in an interview, *how a compiler works* — and a merged PR you can't
explain is worth nothing in that conversation.

### Every pull request body answers three questions

Keep it to five sentences. This is the learning artefact; the diff is just
evidence.

```markdown
**What this does.**   One or two sentences.
**What I learned.**   The concept this story exists to teach.
**Interview answer.** The epic's question, answered in your own words.
```

### Conventions

| Thing | Format | Example |
| ----- | ------ | ------- |
| Branch | `e<N>/s<M>-<slug>` | `e4/s2-mlirgen-skeleton` |
| PR title | `[E<N>.S<M>] <what it does>` | `[E4.S2] MLIRGen: function skeleton and scalar params` |
| Epic issue | `[E<N>] <epic name>` | `[E4] MLIRGen: AST to MLIR` |
| Story issue | sub-issue of its epic | |

Labels — create these once, use them forever:

- `epic:E1-toolchain` … `epic:E12-capstone` — one per epic, for filtering
- `type:epic`, `type:story`, `type:learning` (a story whose output is a
  written note, not code)
- `size:S` (a session or two), `size:M` (a few sessions), `size:L` (a week+)
- `blocked` — and say in the issue what it is blocked on

### Definition of done, for every story

1. It builds.
2. It has a test, and CI runs that test.
3. The PR body answers the three questions above.
4. Nothing that used to pass now fails.

---

## Before you start: what already exists

You have chosen to rebuild from scratch. That is a legitimate way to learn —
rebuilding something you only half-understand is how it sticks. But rebuild
with your eyes open, so two facts first.

**Fact one: the front half of the compiler works today.** `main` has a lexer,
a Pratt parser, an AST and printer, diagnostics, semantic analysis and the
golden interpreter, under **118 passing unit tests**. Epics E2 and E3 below
describe rebuilding exactly that.

**Fact two: none of that is what is stuck.** MLIRGen and the optimisation
pass are written — roughly 1,200 lines — and have *never been compiled*,
because the toolchain was never installed. From MILESTONES.md:

> P0 — MLIR/CIRCT toolchain: **blocked on a source build.** This machine's
> WSL is `aarch64`, so no CIRCT prebuilt applies.

Rewriting the frontend does not unblock the toolchain. **E1 is first, and it
is first whether you rewrite anything else or not.** Until CIRCT builds,
every epic from E4 onward is a hypothesis.

So each epic below carries an **On `main` today** line. When you reach an
epic, look at what is there and pick one:

- **Rewrite** — close the file, build it again from the spec. Best learning,
  slowest.
- **Port** — read it, understand it line by line, bring it across with tests.
  A review PR with `type:learning` is a fine story.
- **Keep** — it works and you can explain it. Move on.

Nothing is ever lost by choosing wrong: the existing code stays in git history
on `main` forever, and you can always diff your rebuild against it — which is
itself one of the better learning exercises available here.

---

## Dependency order

```
E1 toolchain ──┬─→ E2 language ──→ E3 semantics ──→ E4 MLIRGen ──→ E5 optimisation
               │                                          │
               │                                          ├──→ E6 stream dialect
               │                                          │
               └──────────────────────────────────────────┴──→ E7 scheduling
                                                                    │
                                                    E8 hardware ←────┘
                                                        │
                    ┌───────────────────────────────────┼──────────────────┐
                    ↓                                   ↓                  ↓
            E9 pipelining + streams          E10 resource sharing   E11 verification
                    │                                   │                  │
                    └───────────────────────────────────┴──────────────────┘
                                          ↓
                                    E12 capstone
```

E1 blocks everything. E2 and E3 can proceed while a CIRCT build runs in the
background — that build takes hours, so start it and go write a parser.
E9, E10 and E11 are independent of each other once E8 lands.

**A complete project stops after E8.** Everything from E9 on makes it more
impressive; E1 through E8 makes it *real*. If time runs short, a working
E1–E8 beats a half-finished E12.

---

# The epics

## E1 — Toolchain and foundations

**Goal.** MLIR and CIRCT installed, a hand-written circuit going all the way
to a passing Verilator simulation, and a C++ binary of ours that links MLIR.

**Concepts.** What MLIR and CIRCT actually are; out-of-tree LLVM builds; the
RTL simulation and synthesis toolchain.

**Interview questions this prepares.** *What is MLIR, and how does it differ
from LLVM IR? Why would a hardware compiler use it?*

**On `main` today.** `build.ps1`, `scripts/build_circt.sh`, `hw/`,
`tests/cosim/`, a Yosys area script. The build system works; the CIRCT build
does not yet.

| | Story | Size |
|-|-------|------|
| **S1** | **Repo skeleton and build.** Directory layout, CMake, a `minihls` binary that prints `--version`, GoogleTest wired up, a GitHub Actions workflow that builds and runs tests. *Done when:* CI is green on an empty project. | S |
| **S2** | **Settle the aarch64 problem.** Find out whether your WSL is `aarch64` (`uname -m`) and, if so, either install an `x86_64` WSL distro so CIRCT's prebuilt release applies, or commit to the source build. Write down the decision and why. *Done when:* `docs/TOOLCHAIN.md` states the architecture, the choice, and the trade-off. **This is the single most valuable story in the project — everything after E3 waits on it.** | S |
| **S3** | **Build or install MLIR and CIRCT.** Follow S2's decision through to working binaries. Expect 1–2 hours on 8 cores for a source build; use `lld` and limit link parallelism so WSL does not run out of memory. *Done when:* `circt-opt --version` works, and `docs/TOOLCHAIN.md` reproduces it from a clean WSL. | M |
| **S4** | **Install Verilator and Yosys.** OSS CAD Suite is the least painful route. *Done when:* `verilator --version` and `yosys -V` both work. | S |
| **S5** | **Hello, hardware.** Hand-write `max3` as an `hw.module` using `comb`, run `circt-opt --export-verilog`, simulate the result in Verilator, exhaustively for small widths. *Done when:* the simulation passes. You have now seen the last two stages of your own compiler work, before writing any of it. | S |
| **S6** | **Link MLIR from our code.** Out-of-tree CMake `find_package(MLIR)` / `find_package(CIRCT)`; the `minihls` binary registers the dialects and round-trips a `.mlir` file. *Done when:* `minihls dump hello.mlir` prints back what it read. | S |

---

## E2 — The language and its frontend

**Goal.** MiniHLS source text becomes an AST, with good error messages.

**Concepts.** Lexing; recursive descent and precedence climbing; AST design;
diagnostics with source locations.

**Interview questions.** *Walk me through your frontend. How do you parse
expressions with precedence? What makes a good compiler error message?*

**On `main` today.** All of it, tested: `src/frontend/` (14 files),
`LANGUAGE.md` (433 lines), six examples, unit tests.

| | Story | Size |
|-|-------|------|
| **S1** | **The language specification.** Grammar in EBNF, the type system (`iN`/`uN`, arrays, `stream<T>`), the width-growth rules as a table, and the pragmas. The width rules are the distinctive thing about this language: addition grows by one bit, multiplication sums the widths, narrowing must be explicit. *Done when:* every example below is expressible and unambiguous. | S |
| **S2** | **The examples.** `max3`, `abs_diff`, `popcount`, `dot`, `fir`, `stream_sum` — each with hand-computed expected outputs. These six programs are the regression suite for the entire project, from the interpreter to the generated silicon. Write them first; everything downstream is measured against them. | S |
| **S3** | **Fixed-width integer support.** Wrap, truncate, sign- and zero-extend at arbitrary widths. Used by the interpreter and every testbench. *Done when:* unit tests cover the edge widths (1, 63, 64, 65 bits). | S |
| **S4** | **Lexer and diagnostics.** Tokens with source locations, and a diagnostic engine that prints the offending line with a caret. *Done when:* a bad token gives you line, column and an underline. | M |
| **S5** | **Parser and AST.** Recursive descent for statements, precedence climbing for expressions. *Done when:* every example parses, and the AST printer round-trips to equivalent source. | M |

---

## E3 — Semantics and the golden reference

**Goal.** The width rules are enforced, and a reference model exists that
every later stage is checked against.

**Concepts.** Scopes and symbol tables; type checking; constant evaluation;
golden-model testing.

**Interview questions.** *How does type checking work? How do you know your
compiler is correct?*

**On `main` today.** All of it: `src/sema/`, `src/interp/`, `minihls check`,
`minihls run`, 118 unit tests.

| | Story | Size |
|-|-------|------|
| **S1** | **Name resolution and scopes.** A symbol table, block scoping, shadowing rules, and clear errors for undefined and redefined names. | S |
| **S2** | **Type checking and the width rules.** Table-driven, so the rules live in one place: add grows one bit, multiply sums widths, comparison yields `u1`, narrowing requires an explicit cast. *Done when:* a table test covers every operator crossed with every signedness combination. | M |
| **S3** | **Constant evaluation and trip counts.** Prove every loop has a compile-time bound — hardware cannot allocate an unknown number of cycles — and reject the rest with an error that says why. | S |
| **S4** | **The AST interpreter.** Execute the program exactly as the language defines it: compute in wide exact arithmetic, then apply the width rules. This is the golden reference for the rest of the project. *Done when:* all six examples match their hand-computed outputs. | M |
| **S5** | **`minihls run` and the test harness.** The CLI, a test runner that compares against expected-output files, and CI running it on every push. | S |

---

## E4 — MLIRGen: AST to MLIR

**Goal.** `minihls emit-mlir examples/dot.hc` prints valid MLIR.

**Concepts.** `OpBuilder`, locations and types; how a frontend generates SSA;
signless integers; structured control flow; and the gaps between what your
language promises and what the target IR guarantees.

**Interview questions.** *How does a frontend generate SSA? What did you have
to be careful about when lowering your language to someone else's IR?*

**On `main` today.** `src/mlirgen/` — 745 lines plus an LLVM-JIT execution
check and 214 lines of differential tests. **Never compiled.** Treat it as a
hypothesis, not as working code.

| | Story | Size |
|-|-------|------|
| **S1** | **Read MLIR by hand.** *No product code.* Hand-write `max3` and `dot` in `func`/`arith`/`scf`/`memref`. Run `mlir-opt --canonicalize --cse --mlir-print-ir-after-all` and read what changed. Lower `dot` to the LLVM dialect and run it with `mlir-runner`. *Done when:* `docs/NOTES/mlir-by-hand.md` explains what each pass did and what block arguments replace phi nodes. Do not skip this — generating an IR you cannot read is how you spend a week on a bug you could have seen. | S |
| **S2** | **MLIRGen skeleton.** A module, a `func.func`, scalar parameters, constants, a return, and `max3`'s expressions in `arith`. *Done when:* the emitted IR passes `mlir-opt`'s verifier. | M |
| **S3** | **Width rules as explicit casts.** Your width rules become `extsi`/`extui`/`trunci`. MLIR's integers are *signless* — signedness lives in the operation, so you pick `divsi` vs `divui`, `cmpi slt` vs `cmpi ult`. *Done when:* FileCheck tests cover the width table. | M |
| **S4** | **Guards for defined behaviour.** In `arith`, dividing by zero and shifting by at least the bit width are *undefined*; in your language they are defined. So MLIRGen must emit explicit guards. Knowing a lowering has to close gaps like this is a real signal of compiler understanding — this small story is worth talking about. | S |
| **S5** | **`if`/`else` → `scf.if`.** With results, so the structured form carries values out. | S |
| **S6** | **Loops → `scf.for` with `iter_args`.** Loop-carried values — the `acc` in a dot product — become `iter_args`, which are MLIR's structured answer to phi nodes. *Done when:* `dot.hc` emits one clean `scf.for`. | M |
| **S7** | **Arrays → `memref`, constant arrays → globals.** | M |
| **S8** | **Differential test against the interpreter.** Lower the generated MLIR to the LLVM dialect, JIT it, and compare against the AST interpreter on random inputs for every example, in CI. *Done when:* green over thousands of random cases per example. This catches MLIRGen bugs while they are still cheap — before any hardware exists to debug. | M |

---

## E5 — Optimisation

**Goal.** A pass pipeline that cleans up the IR, plus one rewrite pattern
that is yours.

**Concepts.** Pass managers; canonicalisation; rewrite patterns; FileCheck
IR testing.

**Interview questions.** *Walk me through a pass you wrote. What is
canonicalisation and why does it come before everything else?*

**On `main` today.** `src/transforms/` — unrolling, the narrowing pattern,
metrics. Same caveat as E4: written, never compiled.

| | Story | Size |
|-|-------|------|
| **S1** | **Pass pipeline and IR test harness.** Run `canonicalize`, `cse` and `sccp`; set up lit/FileCheck. *Done when:* you can write an IR-in, IR-out test in ten lines. | S |
| **S2** | **`#pragma unroll`.** Use MLIR's existing loop-unrolling utility. The story is the plumbing — reading a pragma off the AST, carrying it through MLIRGen as an attribute, acting on it in a pass. | S |
| **S3** | **The bit-width narrowing pattern.** *Ours.* The low bits of a sum depend only on the low bits of its inputs, so `trunci(addi(extsi a, extsi b))` becomes an add at the narrow width. *Done when:* the dot product's 33-bit intermediate becomes a 32-bit adder, with tests. This is the most common kind of MLIR code there is, and the one an interviewer is most likely to ask you to write on a whiteboard. | M |
| **S4** | **Before-and-after metrics.** Operator counts and total datapath bits, printed per function. You will want these numbers in E10 anyway. | S |

---

## E6 — A dialect of our own: streams

**Goal.** `minihls.stream_read` and `minihls.stream_write` exist, and MLIRGen
emits them for `stream<T>` parameters.

**Concepts.** TableGen/ODS; custom operations and types; op verifiers.

**Interview questions.** *When would you define a new dialect instead of
reusing an existing one?*

**On `main` today.** Nothing.

| | Story | Size |
|-|-------|------|
| **S1** | **ODS dialect skeleton.** A dialect defined in TableGen, wired into the CMake build so the C++ is generated. *Done when:* the dialect registers and an empty op parses. | S |
| **S2** | **The type and the two operations.** `!minihls.stream<T>`, `stream_read`, `stream_write`, each with a verifier that rejects the malformed cases. | S |
| **S3** | **MLIRGen emits them** for `stream<T>` parameters. *Done when:* `stream_sum.hc` emits stream ops and still verifies. | S |

---

## E7 — Scheduling

**This is the heart of the project, and the part you will be asked about
most.**

**Goal.** Every operation gets a start cycle that respects the clock period.

**Concepts.** Scheduling as constraint solving; operator chaining; the
difference between latency and combinational delay; resource constraints.

**Interview questions.** *How does HLS scheduling work? ASAP versus list
scheduling versus SDC? What is the difference between an operator's latency
and its delay, and why does an HLS tool need both?*

**On `main` today.** Nothing. CIRCT provides the problem models and the
solvers; you build the problem and use the answer.

| | Story | Size |
|-|-------|------|
| **S1** | **The delay model.** Nanoseconds per operator and width: adders scale with width, multipliers map to DSP blocks, shifts by a constant are free wiring. Calibrate against Yosys where you can. *Done when:* `docs/DELAY_MODEL.md` has the numbers and says how each was obtained. Made-up numbers are fine to start with, as long as the document admits they are made up. | M |
| **S2** | **A first schedule: ASAP.** Build a CIRCT `Problem` for straight-line code, call the ASAP solver, attach a `start_cycle` attribute to each operation. *Done when:* `max3` schedules and the IR shows the cycles. | M |
| **S3** | **Chaining.** Move to `ChainingProblem`, which knows combinational delays, so independent operations share a cycle when they fit. *Done when:* `max3`'s four operations land in one cycle at a 6.4 ns clock, and split across two when you tighten the clock. That experiment *is* the story. | M |
| **S4** | **Resource limits.** `SharedOperatorsProblem` for memory ports — one read port per `memref`. *Done when:* `dot` respects a single-port memory instead of assuming infinite ports. | M |
| **S5** | **The schedule report.** Per function: cycles, the critical path, and what limited it. *Done when:* the report tells you something you did not already know about one of the examples. | S |

---

## E8 — First hardware

**Goal.** SystemVerilog out of `.hc` for every non-pipelined example, passing
co-simulation. **This is the milestone that makes the project real.**

**Concepts.** FSM plus datapath; register insertion; interface protocols.

**Interview questions.** *What does the RTL your compiler generates look
like, and why? How does it compare to what you would have written by hand?*

**On `main` today.** Nothing, but `tests/cosim/` and `hw/` have the Verilator
pattern to build on.

| | Story | Size |
|-|-------|------|
| **S1** | **Module skeleton and interface.** An `hw.module` with `clk`, `rst`, `start`, `done`, `idle`, scalar inputs latched on `start`, and a result output. Decide the protocol once, write it down, never change it. | M |
| **S2** | **The FSM controller.** One state per scheduled cycle; next-state logic for loops and branches. | M |
| **S3** | **The datapath.** Lower `arith` operations to `comb`. | M |
| **S4** | **Register insertion.** Any value produced in one cycle and used in a later one gets a `seq.compreg`, written in the cycle that produces it. *Done when:* no combinational path spans a cycle boundary. This story is where scheduling stops being an abstraction. | M |
| **S5** | **Memories.** Array parameters become memory ports; constant arrays become ROMs. | M |
| **S6** | **ExportVerilog and generated co-simulation.** CIRCT writes the Verilog; you generate a self-checking Verilator testbench per example, with random inputs and expected values from the AST interpreter. *Done when:* every example passes, in CI. | M |
| **S7** | **Yosys area report.** Cells, LUT equivalents, DSPs and registers, per example, recorded in `docs/RESULTS.md`. You now have numbers, and numbers are what make a portfolio project credible. | S |

---

## E9 — Pipelining and streams

**Goal.** `dot.hc` and `stream_sum.hc` pipelined at their best achievable
initiation interval.

**Concepts.** Modulo scheduling; initiation interval; recurrence-constrained
versus resource-constrained loops; ready/valid handshakes and backpressure.

**Interview questions.** *What limits the II of a loop? What would you change
in the source to improve it?*

| | Story | Size |
|-|-------|------|
| **S1** | **`ModuloProblem` for `#pragma pipeline`.** The loop-carried dependence through `acc` becomes a distance-1 edge. | M |
| **S2** | **Minimum II, and saying why.** Compute the recurrence-bound and resource-bound MII, report requested versus achieved, and name the limiting reason — the accumulator recurrence, or a memory port. *Done when:* the tool explains an II it could not meet, in words. | M |
| **S3** | **The staged datapath.** Per-stage valid bits and a global stall. | L |
| **S4** | **Stream ports.** `valid`/`ready`/`data` for `stream<T>`. | M |
| **S5** | **Co-simulation under random backpressure.** *Done when:* `stream_sum` passes with a driver that stalls at random. Backpressure is where handshake bugs live, and it is exactly what an FPGA team will ask you about. | M |

---

## E10 — Resource sharing and trade-offs

**Goal.** A latency-versus-area curve you can put on a slide and explain.

**Concepts.** Binding; the cost of multiplexing; design-space exploration.

**Interview questions.** *How do you trade area for latency? Why is sharing a
multiplier not free?*

| | Story | Size |
|-|-------|------|
| **S1** | **Constrain and bind.** `SharedOperatorsProblem` with N multipliers; bind operations to physical units and build the input multiplexers. | L |
| **S2** | **The FIR curve.** `fir.hc` with 1, 2 and 4 multipliers; plot latency against Yosys area; every point must co-simulate. *Done when:* `docs/RESULTS.md` has the plot **and an explanation of its shape** — including why the curve bends where it does. | M |

---

## E11 — Verification hardening

**Goal.** Find bugs automatically instead of by hand.

**Concepts.** Differential testing; fuzzing; automatic test-case reduction.

**Interview questions.** *How would you test a compiler? You have a failing
10,000-line program — what now?*

This epic is a genuine differentiator. Most personal compiler projects have
a handful of hand-written tests; almost none have a fuzzer and a reducer.

| | Story | Size |
|-|-------|------|
| **S1** | **Random program generator.** Well-typed random MiniHLS programs. The hard part is generating programs that *type-check*, which will teach you your own type system properly. | M |
| **S2** | **Three-way differential harness.** AST interpreter versus MLIR JIT versus RTL simulation. Any disagreement between the three is a bug in one of them. | M |
| **S3** | **Automatic reduction and a bug log.** Shrink a failing program to its smallest failing form. *Done when:* `docs/BUGS.md` lists each bug found with its reduced reproducer. That document is interview gold. | M |

---

## E12 — Capstone: ITCH parsing

**Goal.** Something a trading-firm FPGA team would recognise immediately.

**Concepts.** Streaming protocol parsing; comparing generated RTL against
hand-written RTL honestly.

**Interview questions.** *Tell me about a project.*

| | Story | Size |
|-|-------|------|
| **S1** | **The parser in MiniHLS.** An ITCH Add Order field extractor over a `stream<u8>`. | M |
| **S2** | **A hand-written RTL baseline.** The same parser, by hand, with the same interface and the same testbench. | M |
| **S3** | **The comparison.** Cycles of latency, area, registers, DSPs and Fmax, side by side — **with a written explanation of every difference.** *Done when:* `docs/CAPSTONE.md` accounts for each gap. Where your compiler loses, saying precisely *why* it loses is worth more than winning. | M |

---

## Optional side quests

Not epics. Pick one up when you want a change of pace, or when an interview
is coming and you want a specific answer ready.

- **An LLVM pass.** Write a small LLVM function pass — count operations and
  estimate their hardware cost — and run it with `opt`. LLVM questions come
  up in interviews too, and this is the quickest route to a real answer.
- **A baseline to compare against.** Run CIRCT's own static HLS flow
  (`hlstool`) on the same examples and compare its output with yours.
- **A scheduler study.** Compare CIRCT's ASAP, simplex and ILP schedulers on
  every example: quality of result against compile time. This is the closest
  thing here to an original result, and it is a week of work, not a month.

---

## An honest note on novelty

As research, this is not novel, and you should say so before anyone asks.
HLS on MLIR already exists — CIRCT's own flows, Dynamatic, ScaleHLS — and
every scheduling algorithm here comes from published work.

It does not need to be novel. As a portfolio project it is distinctive for
four reasons, and these are the four things to lead with:

1. **A language designed around hardware widths**, with the width rules
   carried through every stage and verified bit-exactly. Most student HLS
   projects inherit C's `int` and lose this entirely.
2. **Differential verification throughout** — a golden interpreter, MLIR's
   own execution of the IR, and RTL simulation must all agree, on every
   example, plus a random program generator. This is how real compiler teams
   work and very few personal projects do it.
3. **Streams with backpressure and pipelines with stalls** — the hard part of
   real accelerators, and precisely what trading-firm FPGA teams build.
4. **A measured capstone** — real RTL, compared against hand-written RTL, with
   every difference explained.
