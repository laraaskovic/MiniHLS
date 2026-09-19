# Toolchain

MiniHLS needs MLIR and CIRCT to build anything from epic E4 onward, plus
Verilator and Yosys to check and measure the hardware it generates.

## The problem

CIRCT publishes release binaries for `linux-x64`, `macos-arm64`, `macos-x64`
and `windows-x64`. **There is no `linux-arm64` release.** On Windows-on-ARM,
WSL is `aarch64`, so there is nothing to download.

Check which case you are in before doing anything else:

```bash
uname -m
```

- **`x86_64`** — no problem. Download `circt-full-shared-linux-x64.tar.gz`
  from the [CIRCT releases](https://github.com/llvm/circt/releases), unpack
  it, and skip to [Verilator and Yosys](#verilator-and-yosys).
- **`aarch64`** — read on.

## The route we took: build it in CI

`.github/workflows/build-circt.yml` builds MLIR and CIRCT on GitHub's free
`ubuntu-24.04-arm` runners and uploads the result as an artifact.

**Why this one.** A source build on the laptop works, but it is 2–4 hours of
the machine being unusable, and if it fails at hour three you start again.
Building in CI costs nothing, happens somewhere else, is reproducible by
anyone who clones the repo, and produces a tarball you can re-download when
you break your installation.

### Running it

1. **Actions** tab → **Build CIRCT (linux-arm64)** → **Run workflow**.
2. Leave the CIRCT ref at its default unless you have a reason to change it.
3. Wait. Expect a couple of hours on four cores.
4. Download the `circt-mlir-linux-arm64-*` artifact from the finished run.

```bash
mkdir -p ~/minihls-toolchain && cd ~/minihls-toolchain
unzip ~/Downloads/circt-mlir-linux-arm64-*.zip
tar xzf circt-mlir-linux-arm64.tar.gz
```

Then add this to `~/.bashrc`:

```bash
export MINIHLS_TOOLCHAIN="$HOME/minihls-toolchain/toolchain"
export PATH="$MINIHLS_TOOLCHAIN/bin:$PATH"
export LD_LIBRARY_PATH="$MINIHLS_TOOLCHAIN/lib:$LD_LIBRARY_PATH"
```

`LD_LIBRARY_PATH` is not optional: the build uses shared libraries, which
keeps the build small enough to fit on a runner but means the binaries need
to find their `.so` files at run time.

Verify:

```bash
mlir-opt --version
circt-opt --version
```

### If a run fails or times out

The workflow is two jobs — `llvm` then `circt` — for two reasons. Each gets
its own six-hour budget, and the LLVM install is cached by its commit SHA,
so once LLVM builds successfully it is **never built again**. A failure in
`circt` costs you the CIRCT build only.

Each job also saves its `ccache` with `if: always()`, so a job that is killed
still banks its progress and a re-run resumes rather than restarting.

### What went wrong the first time

The first attempt was OOM-killed after 41 minutes, 3109 steps into 5727:

```
c++: fatal error: Killed signal terminated program cc1plus
```

The cause was compile parallelism. Ninja defaults to `nproc + 2` concurrent
jobs — six on this runner — and MLIR's TableGen-generated files
(`ROCDLDialect.cpp`, `SPIRVOps.cpp`) can each take several GB in a single
translation unit. Six at once does not fit in 16 GB. Capping compile jobs at
two fixed it.

Worse, the OOM killed the job's post-steps too, so the `ccache` was never
saved and all 41 minutes were lost. That is why the cache is now saved by an
explicit step with `if: always()` rather than by the `actions/cache` post-step.

### Points worth understanding

The flags that matter, and they are the same ones that make or break a local
build in WSL:

| Flag | Why |
|---|---|
| `git submodule update --init llvm` | CIRCT pins the exact LLVM commit it needs. A system or apt LLVM will not link. |
| `-DLLVM_PARALLEL_COMPILE_JOBS=2` | **The one that matters.** A single MLIR translation unit can need several GB. Unbounded compile parallelism is what gets `cc1plus` OOM-killed. |
| `-DLLVM_PARALLEL_LINK_JOBS=1` | Linking LLVM takes gigabytes *per job* too. |
| `-DBUILD_SHARED_LIBS=ON` | Static LLVM does not fit in a runner's 14 GB disk, and links take far longer. |

The general lesson, which applies just as much to WSL: **an LLVM build is
limited by memory, not by cores.** Giving it more parallelism than it has RAM
for does not make it faster, it makes it fail.

### The alternatives we did not take

- **Build locally on aarch64.** Same flags, plus raising WSL's memory ceiling
  in `%UserProfile%\.wslconfig` and `wsl --shutdown` to apply it. Works, but
  ties up the laptop.
- **Develop on x86_64** (GitHub Codespaces). The prebuilt tarball applies
  unmodified. Fastest to a working MLIR, but development stops being local.

## Verilator and Yosys

**Verilator** compiles SystemVerilog to C++ and runs it — this is how we
check the generated hardware is *correct*. **Yosys** synthesises it to gates
— this is how we find out how *big* it is. Every number in `docs/RESULTS.md`
comes from Yosys.

The [OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build) bundles
both, including `arm64` builds. Distribution packages are usually too old.

```bash
verilator --version
yosys -V
```

## Nothing here blocks the frontend

Epics E2 and E3 — the language, lexer, parser, AST, semantic analysis and the
golden interpreter — need nothing but a C++ compiler. Start the CIRCT build,
then go and work on those while it runs. The previous attempt at this project
stalled waiting on the toolchain and wrote 1,200 lines that were never once
compiled; do not repeat it.
