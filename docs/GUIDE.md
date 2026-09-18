# A guide to what this project does and what the tools are for

[README.md](../README.md) is the specification: what MiniHLS will be and in what
order it gets built. This document is the orientation: what the problem actually
is, what each tool in the stack does, and how the pieces that exist today fit
together.

If you want the layer below this — why a hardware target needs these tools at
all, how that compares to a compiler targeting x86 or a bytecode VM, and the
operating-system concepts underneath — read
[COMPILERS_AND_SYSTEMS.md](COMPILERS_AND_SYSTEMS.md) first.

- [The problem in one page](#the-problem-in-one-page)
- [Why hardware is a different target](#why-hardware-is-a-different-target)
- [The tools](#the-tools)
- [How we know the output is correct](#how-we-know-the-output-is-correct)
- [What Milestone 0 actually built](#what-milestone-0-actually-built)
- [Running things](#running-things)
- [Glossary](#glossary)

---

## The problem in one page

You can write a dot product in C in about six lines. Getting that same dot
product to run on an FPGA means describing *circuitry*: which multiplier, which
adder, which register, and on which clock cycle each one does its work. Writing
that by hand in SystemVerilog is slow and error-prone.

High-level synthesis is the compiler that bridges the two. It takes the
sequential C-like description and produces a circuit: a **datapath** (the
arithmetic units and registers that hold values), a **controller** (the state
machine that says what happens on which cycle), and a **schedule** (the
assignment of each operation to a cycle) that fits inside a target clock period.

MiniHLS is that compiler, written from scratch. The input is a small C-like
language (`.hc` files); the output is synthesizable SystemVerilog.

```c
i32 dot(i16 a[64], i16 b[64]) {
  i32 acc = 0;
  for (u7 i = 0; i < 64; i = i + 1) {
    acc = acc + a[i] * b[i];
  }
  return acc;
}
```

The interesting question is not "how do I emit Verilog text". It is: *given a
6.4 ns clock, a multiplier that takes 4 ns, and an adder that takes 1.5 ns, how
many operations can I chain into one cycle, how many multipliers do I need, and
how few cycles can the loop take?* That is scheduling, binding, and pipelining,
and it is the actual content of the project.

---

## Why hardware is a different target

If you have only written software, a few assumptions have to go.

**There is no program counter.** Every gate in a circuit is always computing.
`a + b` is not an instruction that executes; it is an adder that permanently
exists and whose output continuously reflects its inputs. Sequencing does not
come free — you build it, out of registers and a state machine.

**The clock is the unit of time.** Logic settles between clock edges. If a chain
of operations takes longer to settle than the clock period, the circuit is
broken — not slow, *broken*, producing garbage. The longest such chain is the
**critical path**, and its length sets the maximum frequency (**Fmax**). This is
why the scheduler must know how long each operator takes: it decides where to
insert registers so no path is too long.

**Loops are not free.** A `for` loop with 64 iterations is not 64 instructions.
It is either one piece of hardware reused 64 times (small, slow), or 64 copies
running at once (large, fast), or a **pipeline** where iteration *n+1* starts
before iteration *n* finishes. That choice is the latency-versus-area trade-off
the project measures.

**Width is not rounded up to 32.** In software, an 8-bit add and a 32-bit add
cost the same. In hardware a 32-bit adder is literally four times the gates of
an 8-bit one. So the language has explicit widths (`i8`, `u7`, `i32`), and
narrowing a value is a real optimization — which is why there is a bit-width
range analysis pass in Milestone 4.

**Arithmetic here does not wrap silently.** MiniHLS grows results instead:
addition gives the wider operand plus one bit, multiplication gives the sum of
the widths. So the operation itself can never overflow, and truncation happens
only where you explicitly assign to a narrower type. That rule is implemented in
[src/support/bits.hpp](../src/support/bits.hpp) and mirrored in the hardware in
[hw/minihls_adder.sv](../hw/minihls_adder.sv).

---

## The tools

### SystemVerilog — the output language

A hardware description language. Despite looking like code, a synthesizable
subset of it describes structure, not steps. `assign sum = a + b;` means "there
is an adder here", permanently.

This is what MiniHLS emits, and what a human would otherwise write by hand. The
adder in [hw/minihls_adder.sv](../hw/minihls_adder.sv) is deliberately hand-written:
it is the reference for what good output looks like, and later it becomes the
baseline that generated RTL is compared against.

### Verilator — simulation (does it compute the right answer?)

**What it is:** a compiler that translates synthesizable SystemVerilog into C++,
which you then compile and link into your own program. You get a C++ class with
the module's ports as member variables and an `eval()` method.

**Why we use it:** to check that a circuit *computes the correct values*. It is
extremely fast because it is cycle-based rather than event-driven, which matters
when you want to run millions of random test vectors.

**How it looks in practice.** `verilate()` in
[tests/cosim/CMakeLists.txt](../tests/cosim/CMakeLists.txt) turns `adder8.sv` into a
`Vadder8` C++ class. The testbench then does this:

```cpp
dut.a = 5;
dut.b = -3;
dut.eval();          // let the combinational logic settle
// dut.sum is now 2
```

For combinational logic you set inputs and call `eval()`. For clocked designs you
will toggle `clk` and call `eval()` twice per cycle — that is how the FSM-based
hardware from Milestone 5 gets tested.

**What it is not:** Verilator is not a general-purpose simulator. It handles the
*synthesizable* subset, and by default it is **2-state** — signals are 0 or 1,
with no modelling of the `X` (unknown) and `Z` (high-impedance) values that
commercial simulators like Questa or VCS track. That is usually what you want
here, but it means Verilator will not catch a bug where real hardware would
power up with an uninitialized register.

### Yosys — synthesis (how much hardware is it?)

**What it is:** an open-source synthesis suite. It reads RTL and lowers it to a
**netlist** — a graph of concrete gates and flip-flops. This is the same class of
transformation that turns C into machine code, except the target is circuitry.

**Why we use it:** for a fast, free answer to "how big is this?". The
[scripts/area_report.sh](../scripts/area_report.sh) sweep prints:

```
WIDTH=4  ->  21 cells
WIDTH=8  ->  50 cells
WIDTH=16 -> 111 cells
```

Growth is roughly linear in width, which is what you expect from a ripple-carry
structure: each bit needs its own full adder. This is the seed of the operator
area library that the scheduler needs in Milestone 5 — the only honest way to
know what an operator costs at a given width is to synthesize it.

**The passes in our script**, from
[characterize/yosys/cell_count.ys](../characterize/yosys/cell_count.ys):

| Pass        | What it does                                                    |
| ----------- | --------------------------------------------------------------- |
| `read_verilog` | parse the SystemVerilog                                      |
| `hierarchy` | resolve module instantiations, pick the top module, fix parameters |
| `proc`      | convert procedural blocks (`always`) into logic and registers    |
| `opt`       | constant folding, dead logic removal — a real optimizer          |
| `techmap`   | lower generic operators (`$add`) into actual gates               |
| `stat`      | print the counts                                                 |

`techmap` is the important one. Before it, an adder is a single abstract `$add`
cell and every width looks identical. After it, you get gates, and widths become
comparable.

**An important caveat.** These are *generic gate* counts, not FPGA resources. An
FPGA is not built from AND/OR gates; it is built from **LUTs** (small lookup
tables) and flip-flops. A generic gate count is a decent proxy for comparing two
designs, but it is not the number the Milestone 9 comparison needs. For real LUT
counts you either run Yosys with an FPGA-specific flow (`synth_xilinx`) or use
the vendor tool.

### Quartus — timing closure (how fast will it actually run?)

Intel/Altera's toolchain, and the one this project targets. It does synthesis,
then **place and route** (Quartus calls it the *fitter*) — deciding which
physical logic element on the chip each piece of logic lands in and how the wires
get there — and then `quartus_sta` reports real timing, including wire delay.
That is where a trustworthy Fmax number comes from.

Quartus reports resources as **ALMs** (adaptive logic modules), registers, **DSP
blocks**, and **M10K/M20K** memory blocks, rather than the LUT/FF vocabulary
Xilinx uses.

**Where it runs.** Not on this laptop — it's `aarch64`, and no FPGA vendor ships
an ARM build. Quartus lives on the university lab machines, which also have
**Questa** for simulation. That is fine, and it shapes the design in one
important way:

> The operator delay and area library from Milestone 5 should be a **committed
> data file**, not a live tool invocation. Something like
> `characterize/data/cyclone5.json`, regenerated occasionally by a Tcl script on
> a lab machine and read by the compiler. That way MiniHLS schedules correctly on
> a laptop with no vendor tools installed, and the target becomes swappable.

AMD/Xilinx's **Vivado** is the direct equivalent if you ever have access; it
speaks LUTs instead of ALMs and is what the separate ITCH RTL project used.

### Questa — the strict second opinion

Siemens' simulator (the successor to ModelSim), also on the lab machines. It is
event-driven and **4-state**: it models `X` (unknown) and `Z` (high-impedance),
which Verilator does not.

That closes a real hole. A generated FSM whose reset does not cover every state
register works perfectly in Verilator's 2-state world and emits garbage on real
silicon. Incomplete reset in emitted controllers is a genuine HLS bug class, and
a 4-state simulator is what catches it.

Questa does *not* replace Verilator here. Milestone 8's fuzzing campaign needs
thousands of runs and has to work on this laptop; Questa is slower and
license-bound to lab machines. So: Verilator for the inner loop, Questa for a
small deliberate suite over the examples checking reset and X-propagation.

Worth knowing: **cocotb supports Questa as a backend**, so the Python
golden-model pattern from the separate ITCH RTL project transfers directly rather
than needing a second harness written from scratch.

### The division of labour

| Tool | Answers | Runs on | How often |
| --- | --- | --- | --- |
| **Verilator** | does it compute the right values? | this laptop | every commit, millions of vectors |
| **Yosys** | roughly how big is it? | this laptop | every build |
| **Questa** | does it survive X-propagation and reset? | lab machine | occasionally, examples only |
| **Quartus** | real ALMs, DSPs, and Fmax | lab machine | per-milestone characterization |

Knowing what each tool can and cannot tell you is most of practical verification
judgment. Yosys will never give you a trustworthy Fmax; Verilator will never tell
you about an uninitialized register; Quartus will never run fast enough to fuzz
with.

**One caveat on the clock target.** The README's default is 6.4 ns (156.25 MHz,
the 10GbE datapath clock). On a Cyclone V — the DE1-SoC family — that is
reachable but genuinely tight for wide arithmetic unless multiplies map to DSP
blocks and the design is well pipelined. On an older Cyclone IV it is harder
still; on Arria 10 or Agilex it is comfortable. Measure a trivial design at
Milestone 5 rather than discovering it at Milestone 9. The target is a CLI flag,
so relaxing it is a one-line change, not a redesign.

### CMake, CTest, GoogleTest — building and testing

Three separate things that are easy to conflate:

- **CMake** reads [CMakeLists.txt](../CMakeLists.txt) and generates real build files
  (Makefiles here). It also fetches GoogleTest and invokes Verilator.
- **GoogleTest** is the C++ test framework — the `TEST(...)` macros, the
  assertions, the pass/fail output.
- **CTest** is CMake's test *runner*. It knows about two tests, `unit` and
  `cosim`, because [tests/unit/CMakeLists.txt](../tests/unit/CMakeLists.txt) and
  [tests/cosim/CMakeLists.txt](../tests/cosim/CMakeLists.txt) register them.

So `100% tests passed, 0 tests failed out of 2` from CTest means two
*executables* passed. Inside them are 15 GoogleTest cases. Run a binary directly
to see the finer breakdown.

### WSL, and why the build happens there

The toolchain — g++, CMake, Verilator, Yosys — is installed in WSL Ubuntu, not on
Windows. This machine has no Windows C++ compiler at all, and Verilator is
awkward on Windows regardless. The repository stays on the Windows filesystem so
your editor sees it normally, and WSL reaches it through `/mnt/c`.

[build.ps1](../build.ps1) exists so that the Windows-side command is still one step;
it maps the path and delegates to WSL.

One consequence: that `/mnt/c` mount is slow, because it is a network-style
filesystem rather than a real one. So the build tree defaults to
`~/.cache/minihls/build` *inside* WSL rather than `./build`. Set `BUILD_DIR` if
you want it somewhere visible from Windows.

### ccache

Shows up in the configure output because Verilator's CMake integration looks for
it. It caches compiled object files, so rebuilding something you have built
before is near-instant. Nothing to configure.

---

## How we know the output is correct

This is the part of the project design that matters most, because a compiler that
silently emits *wrong hardware* is worse than useless.

**The strategy is differential testing against references that are simpler than
the thing being tested.** Two interpreters get built before any hardware is
generated:

1. An **AST interpreter** — walks the parsed source directly.
2. An **IR interpreter** — runs the compiler's internal representation, and is
   re-run after *every* optimization pass.

Both implement exact bit-width semantics. Then generated RTL is simulated in
Verilator and compared against them. The payoff is fault localization:

- IR interpreter disagrees with AST interpreter → the bug is in lowering or in
  whichever pass just ran.
- Both interpreters agree but the RTL disagrees → the bug is in scheduling,
  register insertion, or emission.

That narrowing is worth far more than a single end-to-end pass/fail.

**Co-simulation** is the name for that last comparison: run hardware and a
software model on identical stimulus and require bit-exact agreement. The pattern
in [tests/cosim/adder_cosim_test.cpp](../tests/cosim/adder_cosim_test.cpp) is the one
every later milestone reuses.

**A green test suite that cannot go red proves nothing.** After Milestone 0 built
out, the sign extension was deliberately removed from the adder and the suite was
re-run: it failed, as it should, and passed again once reverted. Cheap to do, and
it is the difference between "the tests pass" and "the tests work".

Where the stimulus comes from, in increasing order of confidence:

- **Exhaustive**, where the space is small enough. The width-8 adder is checked
  against all 65536 input pairs — no sampling, no luck involved.
- **Corner cases**, chosen deliberately. A dropped carry or a missing sign
  extension shows up at the extremes of the range, which random sampling is
  unlikely to hit.
- **Random, with a fixed seed**, for spaces too large to enumerate. Fixed,
  because a co-simulation failure you cannot reproduce is not actionable.
- **Fuzzing** entire random programs, in Milestone 8, with delta debugging to
  shrink any failure to a minimal reproducer.

---

## What Milestone 0 actually built

No compiler yet. The point was to prove the *infrastructure* works end to end, so
that later milestones are only ever debugging one new thing at a time.

```
   hw/minihls_adder.sv                   src/support/bits.hpp
   (hand-written hardware)               (the same rule, in C++)
            |                                      |
     Verilator: SV -> C++                          |
            |                                      |
       Vadder8 / Vadder16  ------>  co-simulation  <-- reference values
                                    (identical stimulus, bit-exact compare)
            |
      Yosys: SV -> gates  ------->  cell counts (area)
```

The adder is the smallest thing that exercises every link in the chain, and it
is not entirely trivial: it implements the width rule, so the C++ reference and
the hardware have to agree on sign extension — exactly the class of mistake this
harness exists to catch.

What exists now:

| File | Role |
| ---- | ---- |
| [hw/minihls_adder.sv](../hw/minihls_adder.sv) | the hand-written adder, parameterized by width |
| [hw/adder8.sv](../hw/adder8.sv), [hw/adder16.sv](../hw/adder16.sv) | per-width wrappers (a Verilated model has exactly one top module) |
| [src/support/bits.hpp](../src/support/bits.hpp) | fixed-width integer semantics: truncate, sign/zero extend, ranges, width rules |
| [tests/unit/support/bits_test.cpp](../tests/unit/support/bits_test.cpp) | 12 tests on those semantics |
| [tests/cosim/adder_cosim_test.cpp](../tests/cosim/adder_cosim_test.cpp) | 3 co-simulation tests: exhaustive, random, corners |
| [scripts/build_and_test.sh](../scripts/build_and_test.sh) | the one command |
| [scripts/area_report.sh](../scripts/area_report.sh) | Yosys cell counts across widths |

`bits.hpp` is not scaffolding. The width rules, truncation, and range helpers in
it are what the type checker, both interpreters, and the range analysis pass will
all call. Getting them right and tested now means later milestones inherit a
reference they can trust.

---

## Running things

```powershell
.\build.ps1                 # from Windows
```

```bash
bash scripts/build_and_test.sh    # from WSL or Linux
```

Either one configures, builds, runs both suites, and prints the area report.

```bash
# Build type and location
BUILD_TYPE=Debug bash scripts/build_and_test.sh
BUILD_DIR=./build bash scripts/build_and_test.sh   # keep artifacts visible from Windows

# One suite only
ctest --test-dir ~/.cache/minihls/build -R cosim --output-on-failure

# Individual GoogleTest cases, with names
~/.cache/minihls/build/tests/cosim/minihls_cosim_tests
~/.cache/minihls/build/tests/cosim/minihls_cosim_tests --gtest_filter='*Exhaustive*'

# Area at other widths
WIDTHS="8 12 24 32" bash scripts/area_report.sh
```

Two things that will bite you, both already worked around in this repo but worth
knowing:

- **Verilator treats a comment starting with the token `verilator` as a
  pragma.** A perfectly innocent `// Verilator elaborates...` comment is a hard
  elaboration error. Reword such comments.
- **`yosys -q` suppresses the `stat` report itself**, not just the banner. Use
  `-Q -T` to drop the banner and footer while keeping the output.

---

## Glossary

**RTL** (register-transfer level) — describing hardware as registers plus the
logic between them. The abstraction SystemVerilog targets and MiniHLS emits.

**Netlist** — a graph of concrete gates and flip-flops. What synthesis produces.

**LUT** (lookup table) — the basic programmable logic element of an FPGA, a small
truth table. FPGA area is measured in LUTs and flip-flops, not gates.

**FF** (flip-flop) — a one-bit register; stores a value between clock edges.

**Critical path** — the longest logic delay between two registers. Sets Fmax.

**Fmax** — the highest clock frequency at which the circuit still works.

**Latency** — how many cycles from input to result.

**Throughput / II** (initiation interval) — how many cycles between successive
loop iterations or inputs entering a pipeline. `II=1` means one new iteration
every cycle, the ideal. II is limited by loop-carried dependencies and by memory
port conflicts.

**Datapath** — the arithmetic units, muxes, and registers that move and transform
data.

**FSM** (finite state machine) — the controller that drives the datapath,
exposing `start` / `done` / `idle`.

**Scheduling** — assigning each operation to a clock cycle. **ASAP** schedules
everything as early as dependencies permit; **ALAP** as late as possible. The gap
between the two is an operation's **mobility** — the slack a smarter scheduler
can exploit.

**Binding** — deciding which physical unit performs which operation. Two
multiplications in different cycles can share one multiplier; that is a smaller,
slower design.

**Operation chaining** — putting several dependent operations in the *same* cycle
because their combined delay still fits the clock period.

**Combinational vs sequential** — combinational logic has no memory and settles
to a function of its inputs (our adder). Sequential logic contains registers and
its output depends on history.

**SSA** (static single assignment) — an IR form where every value is assigned
exactly once. It makes dataflow analysis straightforward.

**Phi node** — an SSA construct meaning "this value comes from one predecessor
block or another". In hardware it becomes a **multiplexer**. This correspondence
is a large part of why SSA suits HLS.

**ready/valid** — the standard two-signal handshake for streaming. The producer
asserts `valid` when it has data; the consumer asserts `ready` when it can accept
it; transfer happens when both are high. **Backpressure** is the consumer holding
`ready` low to stall the producer — which generated pipelines must survive, hence
the random-backpressure test in Milestone 7.

**ROM** — read-only memory. A `const` array becomes one.

**Co-simulation** — running hardware simulation and a software reference model on
the same stimulus and requiring identical results.

**Elaboration** — resolving the module hierarchy and parameters into a single
flat design, before synthesis or simulation.

**ITCH** — Nasdaq's market data feed protocol. The Milestone 9 capstone parses
its Add Order message, which is a genuinely useful thing to build in hardware and
a fair comparison against hand-written RTL.
