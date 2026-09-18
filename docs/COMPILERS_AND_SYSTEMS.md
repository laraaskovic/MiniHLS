# Why this project needs hardware tools, and what compilers usually target

Two questions this answers:

1. Do we need Verilator, Yosys, and Quartus *only* because we compile to hardware?
   Do other compiler projects avoid this?
2. What are the operating-system concepts sitting underneath all of it?

[docs/GUIDE.md](GUIDE.md) explains what each tool does. This document is about
*why the stack looks the way it does*, and the systems background that makes it
make sense.

- [The short answer](#the-short-answer)
- [What compilers actually target](#what-compilers-actually-target)
- [What happens to a normal program](#what-happens-to-a-normal-program)
- [Operating system concepts](#operating-system-concepts)
- [The vocabulary collision](#the-vocabulary-collision)
- [Why our language has no pointers or recursion](#why-our-language-has-no-pointers-or-recursion)
- [Our tools and their software equivalents](#our-tools-and-their-software-equivalents)
- [Compiler projects that need no hardware tools](#compiler-projects-that-need-no-hardware-tools)
- [What is genuinely harder about hardware](#what-is-genuinely-harder-about-hardware)

---

## The short answer

Yes — we need Verilator, Yosys, and Quartus because the target is a circuit, and
those are the tools that simulate and physically realize circuits.

But the more useful answer is this: **every compiler depends on a large
toolchain. Ours is just the only one you can see.**

When you run `gcc hello.c -o hello`, gcc does not produce a runnable program by
itself. It invokes a preprocessor, emits assembly, hands that to an **assembler**
(`as`) to get an object file, and hands *that* to a **linker** (`ld`) along with
the C standard library to produce an executable. Then an **operating system
loader** maps it into memory, resolves shared libraries, and starts it. Then a
**CPU** — a fixed piece of silicon someone else designed and verified — actually
runs it.

That is at least as many moving parts as our stack. You do not notice because
they came pre-installed and are invoked automatically, and because the CPU was
built in a factory years ago rather than by you, last Tuesday.

Compiling to hardware removes the "someone else already built the machine" step.
There is no CPU waiting to execute your output. You are *building the machine*,
so you need tools that can simulate a machine that does not exist yet
(Verilator), turn a description into gates (Yosys), and lay those gates onto real
silicon while proving the timing works (Quartus).

---

## What compilers actually target

"Compiler" just means "translator". What makes one project feel different from
another is mostly the target.

| Target | Example | What you need to test it |
| --- | --- | --- |
| **Machine code** | C → x86-64 or RISC-V | an assembler and linker, or emit them yourself; QEMU to run cross-compiled code |
| **Bytecode for a VM you write** | a Python-like language → your own interpreter | nothing but a C++ compiler |
| **An existing VM** | Kotlin → JVM bytecode, or → WebAssembly | that VM: a JVM, or a wasm runtime like `wasmtime` |
| **Another high-level language** | TypeScript → JavaScript ("transpiler") | the target language's runtime |
| **GPU code** | CUDA, or a shader compiler | NVIDIA's toolchain, or a GPU driver and Vulkan |
| **Hardware** | MiniHLS: `.hc` → SystemVerilog | Verilator, Yosys, Quartus |

Notice that only the bytecode-VM row genuinely needs nothing. Every other target
requires somebody's toolchain, because every target is a machine, and you need
that machine — or a simulation of it — to check your output.

Hardware is at the far end of the spectrum, not off the chart. GPU and embedded
cross-compiler projects sit much closer to us than you'd expect.

---

## What happens to a normal program

This is the part that's worth knowing regardless of hardware, because it's what
"compiling" actually means in the ordinary case.

```
hello.c
   |  preprocessor      #include, #define expanded; output is still C
   v
hello.i
   |  compiler proper   parse -> AST -> IR -> optimize -> select instructions
   v
hello.s                 assembly: human-readable text, one line per instruction
   |  assembler (as)    text -> binary encodings
   v
hello.o                 object file: machine code + a table of unresolved names
   |  linker (ld)       combine .o files, resolve names, lay out the address space
   v
hello                   executable (ELF on Linux, PE on Windows, Mach-O on macOS)
   |  loader (OS)       map into memory, load shared libraries, jump to entry
   v
a running process
```

Three ideas here matter for understanding our project by contrast.

**An object file is incomplete.** It contains machine code with holes. If your
code calls `printf`, the compiler doesn't know where `printf` will live, so it
leaves a placeholder and a note saying "patch this once you know". Those notes
are **relocations**, and the names involved are **symbols**. The linker fills
them in.

**Linking decides layout.** The linker assigns every function and global a final
address and patches all the relocations to match. This is strikingly similar to
what Quartus's *fitter* does: decide which physical location on the chip each
piece of logic occupies, then route the wires accordingly.

**Assembly is a real intermediate stage.** `hello.s` is text, readable, one level
above raw bytes, and a separate tool lowers it further. **SystemVerilog plays
exactly this role for us.** MiniHLS emits SystemVerilog text the way gcc emits
assembly text; Yosys and Quartus are our assembler and linker.

---

## Operating system concepts

The background that makes the above make sense.

### Kernel and user space

The CPU can run in a privileged mode or an unprivileged one. The **kernel** — the
core of the OS — runs privileged: it can touch hardware, configure memory, talk
to disks. Your program runs unprivileged, in **user space**, and cannot do any of
that directly.

When your program needs something only the kernel can do — read a file, allocate
memory, send a packet — it makes a **system call**. That's a special instruction
(`syscall` on x86-64, `svc` on ARM) that traps into the kernel, which does the
work and returns. `printf` is ordinary library code that eventually performs a
`write` syscall.

This boundary is why a program cannot crash the machine, and why it can't reach
another program's memory.

### Processes and virtual memory

A **process** is a running program plus everything the OS tracks about it. Its
most important property is its **address space**: the range of memory addresses
it can use.

The trick is that those addresses are fake. The CPU has a **memory management
unit (MMU)** that translates the addresses your program uses (*virtual*) into
real locations in RAM (*physical*), using per-process lookup tables called **page
tables**. Memory is handled in fixed chunks, usually 4 KB, called **pages**.

Consequences:

- Every process believes it owns the whole address space, starting at the same
  addresses. Two programs both using address `0x400000` are touching different
  physical RAM.
- Processes are isolated from each other for free — a process has no way to name
  another's memory.
- Memory can be *lazily* provided. The OS can promise memory and only find real
  RAM when you first touch it, or move pages out to disk (**swap**).

**There is none of this in a circuit.** No MMU, no pages, no translation. When
MiniHLS generates a memory, it is a specific physical block of RAM on the FPGA
with a specific number of words and a specific number of ports. Addresses are
real and finite.

### Stack and heap

Two kinds of memory inside a process:

The **stack** holds function call frames — local variables, the return address,
saved registers. It grows and shrinks automatically as you call and return. It is
what makes **recursion** possible: each nested call gets a fresh frame, and the
depth is limited only by how much the OS let the stack grow.

The **heap** is memory you request explicitly (`malloc`, `new`) and get back a
**pointer** to. It lives until you free it. Its size isn't known at compile time.

Both depend on being able to grow at runtime, which depends on the OS handing out
pages on demand.

### Threads and concurrency

A **thread** is an independently scheduled flow of execution. Multiple threads in
one process share the address space — same heap, same globals, separate stacks.
Sharing is why concurrency is hard: two threads writing the same variable need
**synchronization** (mutexes, atomics) or the result is undefined.

### The OS scheduler

More runnable threads than CPU cores is normal. The **scheduler** picks which
thread runs on which core and for how long, then performs a **context switch** —
saving one thread's registers, restoring another's. On a **preemptive** OS this
happens whether your code cooperates or not, typically every few milliseconds.

Keep this one in mind; it collides badly with the HLS meaning of "scheduling",
which is the next section.

### Filesystems, and why `/mnt/c` is slow

A filesystem maps names to bytes on a device, and the kernel exposes it through
syscalls like `open`, `read`, `stat`.

This explains something you've already seen in this project. WSL2 is a real Linux
kernel in a lightweight VM. Its own filesystem is local and fast. But
`/mnt/c` — your Windows drive — is reached through a **network-style file
protocol (9P)**: every file operation becomes a message to a server process on
the Windows side and a reply back.

A build does enormous numbers of tiny file operations — stat every header, read
every source, write every object. Each one pays that round trip. That is why
[scripts/build_and_test.sh](../scripts/build_and_test.sh) puts the build tree in
`~/.cache/minihls/build` inside the Linux filesystem instead of `./build`. The
source stays on `/mnt/c` so your editor sees it; the thousands of intermediate
files do not.

### Is there an OS on the FPGA?

Not in the logic we generate. A MiniHLS module is pure circuitry — no kernel, no
processes, no syscalls, no scheduler. It has a `start` input and a `done` output.

With one caveat worth knowing, since it's likely relevant to your lab boards: a
**SoC FPGA** like the Cyclone V on a DE1-SoC has real ARM CPU cores next to the
programmable logic, and those cores can run full Linux. So the *chip* may be
running an OS that talks to your accelerator over a bus. But your generated logic
isn't running on that OS — it's a peripheral the OS talks to.

---

## The vocabulary collision

The single most confusing thing about moving between these worlds: important
words mean different things. Sorting this out early saves a lot of grief.

| Word | In OS / software | In HLS / hardware |
| --- | --- | --- |
| **scheduling** | the OS decides which thread runs next, at runtime, for fairness, over milliseconds | the *compiler* decides which clock cycle each operation happens on, at compile time, permanently, in nanoseconds |
| **register** | one of ~16–32 named CPU slots; register allocation = which variable lives in which slot | a physical bank of flip-flops that you instantiate; you can have thousands, any width you like |
| **allocation / binding** | getting memory from the heap | deciding which physical adder performs which addition, so units can be shared |
| **pipeline** | a fixed property of the CPU you bought (fetch, decode, execute) | something *you build*; depth and initiation interval are your design decisions |
| **latency** | fuzzy, measured in microseconds, varies run to run | an exact integer number of clock cycles, identical every time |
| **memory** | one flat virtual address space, paged, automatically cached | discrete physical blocks, fixed capacity, a countable number of ports per cycle |
| **stack** | grows on demand, holds call frames, enables recursion | does not exist |
| **thread** | an OS-scheduled unit of execution you create to get parallelism | unnecessary — everything is already simultaneous by default |
| **cache** | automatic, transparent, managed by hardware you don't control | nothing is automatic; if you want a buffer you build it and manage it |

The one that trips people most is **scheduling**. An OS scheduler makes runtime
decisions for fairness. An HLS scheduler makes a compile-time decision that is
then frozen into a state machine. Same word, nearly opposite character.

---

## Why our language has no pointers or recursion

[README.md](../README.md) says the language has "no pointers, recursion, dynamic
memory, or floating point". Those aren't arbitrary simplifications to make the
project easier. Each one is missing because the hardware cannot express it.

**No recursion**, because recursion needs a stack of unknown depth. A circuit has
a fixed, finite amount of storage decided at compile time. "However deep this
goes at runtime" has no physical realization. (Hardware also has no return
address to jump back to — there's no program counter to restore.)

**No dynamic memory**, because `malloc` asks an operating system for more pages.
There is no OS and no more pages. The memory on the chip is a fixed set of blocks
decided when the design is compiled.

**No pointers**, for two reasons. Physically, hardware memory isn't a single flat
address space — it's separate blocks, each with its own ports. And analytically,
pointers can **alias**: given `*p = 1; x = *q;` the compiler cannot generally
know whether `p` and `q` refer to the same location, so it cannot know whether
those operations can happen in the same cycle. Scheduling depends absolutely on
knowing which operations are independent. Aliasing destroys that knowledge.

**Constant loop bounds**, because the loop must be unrollable or turned into a
fixed state machine at compile time.

So the language design is downstream of the target. This is normal: every
language's restrictions encode assumptions about the machine underneath.

---

## Our tools and their software equivalents

| Our stack | Software equivalent | Job |
| --- | --- | --- |
| MiniHLS | clang's frontend and middle end | source → IR → optimize → emit target code |
| SystemVerilog | assembly (`.s`) | readable text one level down, lowered by another tool |
| **Yosys** | the assembler, plus instruction selection | text description → concrete primitives (gates, or machine code) |
| **Quartus** fitter | the linker and loader | assign final physical locations, connect everything up |
| `quartus_sta` | — *(no real equivalent)* | prove the result is fast enough to be correct at all |
| **Verilator** | running the binary, or QEMU | execute it and see whether the answers are right |
| **Questa** | Valgrind, or a sanitizer build | slower, stricter, catches what the fast path misses |

The row with no software equivalent is the interesting one. In software, if your
program is too slow it's still *correct*. In hardware, if the logic doesn't
settle within the clock period, the output is garbage. **Timing is a correctness
property.** Static timing analysis has no counterpart in software compilation
because software has no equivalent failure mode.

---

## Compiler projects that need no hardware tools

If you ever want the same intellectual content without the EDA stack, these are
the standard options:

**A C compiler targeting x86-64 or RISC-V.** You write frontend, IR, optimizer,
register allocator, and instruction selection. Emit assembly text and let `as`
and `ld` finish the job — so your only dependency is a toolchain you already
have. Test with QEMU if cross-compiling. Shares almost everything with MiniHLS
except that scheduling means instruction scheduling and registers are a scarce
fixed set.

**A bytecode VM and its compiler.** Source → your own bytecode → your own
interpreter loop. Genuinely zero external dependencies. This is the purest
"compilers only" project.

**A JIT compiler.** Generate machine code into memory at runtime and jump to it.
Needs no toolchain but leans hard on OS concepts: you must `mmap` a region with
execute permission, and deal with W^X restrictions and instruction-cache
coherency. A good way to learn the OS material above.

**A WebAssembly backend.** Target wasm instead of native. The toolchain is a
runtime like `wasmtime`, plus a browser. Very portable, easy to demo.

**A static analyzer or type checker.** All the frontend and dataflow analysis,
no code generation at all.

What none of these give you is the hardware-specific content: physical resource
constraints, timing as correctness, and the latency-versus-area trade-off. Which
is precisely why this project is a differentiator for FPGA and hardware-compiler
roles — and also why it costs more setup.

---

## What is genuinely harder about hardware

An honest list of what the target actually costs us:

1. **Timing is correctness, not performance.** Blow the clock period and you get
   wrong answers, not a slow program. This is why the operator delay library and
   scheduling exist at all.

2. **You cannot just run it.** There is no machine to execute the output. Every
   functional check is a simulation of a machine that doesn't exist yet, which is
   why co-simulation against a software reference model is the whole verification
   strategy rather than an afterthought.

3. **Resources are hard limits.** A chip has a specific number of DSP blocks and
   memory blocks. Exceeding them isn't slow — it fails to build. Software just
   uses more RAM.

4. **Everything is parallel by default.** The hard problem is imposing *order*,
   not finding concurrency. This inverts the usual instinct.

5. **The tools are proprietary and enormous.** Quartus and Vivado are
   multi-gigabyte, licensed, platform-restricted installs. That is why this
   project deliberately leans on Verilator and Yosys — free, fast, scriptable —
   and treats vendor tools as an occasional measurement step rather than part of
   the build. It's also why the operator characterization data is meant to be
   committed to the repo rather than regenerated every build.

Point 5 is a practical engineering judgment, not a hardware fact — but it shapes
this repository more than anything else on the list.
