#!/usr/bin/env bash
# scripts/demo.sh — run one .hc file through every stage that exists, with the
# output of each one labelled. This is the "does it work end to end" command,
# and it is also the thing to show someone: four stages, each one readable.
#
#   ./scripts/demo.sh                      # defaults to examples/max3.hc
#   ./scripts/demo.sh examples/abs_diff.hc
#   ./scripts/demo.sh examples/max3.hc 3 9 5     # extra args go to `run`
#
# Combinational examples only, for now: max3 and abs_diff. Anything with a loop
# or an array stops at emit-mlir, because a loop needs a cycle count and so
# needs E7's scheduler and E8's FSM.

set -u -o pipefail
cd "$(dirname "$0")/.."            # repo root, wherever this was called from

SRC="${1:-examples/max3.hc}"
shift || true
ARGS=("$@")

BUILD="${MINIHLS_BUILD:-build-mlir}"
BIN="$BUILD/src/minihls"

bar() { printf '\n\033[1;36m== %s \033[0m%s\n\n' "$1" "$(printf '=%.0s' $(seq 1 $((60 - ${#1}))))"; }
note() { printf '\033[2m%s\033[0m\n' "$1"; }

if [[ ! -x "$BIN" ]]; then
  echo "no binary at $BIN" >&2
  echo >&2
  echo "Configure and build first. If you have configured before, the MLIR and" >&2
  echo "CIRCT paths are still in the cache and this is enough:" >&2
  echo "    cmake --build $BUILD -j4" >&2
  echo >&2
  echo "If not, recover the paths you used and configure again:" >&2
  echo "    grep -E '^(MLIR|CIRCT)_DIR' $BUILD/CMakeCache.txt" >&2
  echo "    cmake -S . -B $BUILD -DMINIHLS_ENABLE_MLIR=ON \\" >&2
  echo "          -DMLIR_DIR=<...>/lib/cmake/mlir -DCIRCT_DIR=<...>/lib/cmake/circt" >&2
  exit 1
fi
if [[ ! -f "$SRC" ]]; then echo "no such file: $SRC" >&2; exit 1; fi

bar "1. SOURCE  $SRC"
cat "$SRC"

bar "2. FRONT END   lexer, parser, resolver, type checker, const-eval"
"$BIN" check "$SRC" || exit 1

if [[ ${#ARGS[@]} -gt 0 ]]; then
  bar "3. REFERENCE INTERPRETER   minihls run ${ARGS[*]}"
  note "The golden model. Everything below has to agree with this number."
  "$BIN" run "$SRC" "${ARGS[@]}" || exit 1
else
  bar "3. REFERENCE INTERPRETER   the hand-computed vectors"
  note "Numbers from ${SRC%.hc}.tests, computed on paper, never by this project."
  "$BIN" test "$SRC" || exit 1
fi

bar "4. MLIR   func + arith + scf"
note "A dataflow graph. Says WHAT operations exist and what depends on what."
note "No clock, no cycle, no register anywhere in it."
"$BIN" emit-mlir "$SRC" || exit 1

bar "5. CIRCT   hw + comb"
note "The same graph as a circuit. comb has no state, so all of this settles"
note "within one clock period: scf.if became comb.mux, arith.addi became"
note "comb.add, and arith.trunci became comb.extract, which costs no gates."
if ! "$BIN" emit-hw "$SRC"; then
  echo
  note "Not combinational — stops here until E7 and E8. That is expected for"
  note "anything with a loop or an array."
  exit 0
fi

bar "6. SYSTEMVERILOG   via CIRCT ExportVerilog"
OUT="hw/$(basename "${SRC%.hc}").generated.sv"
mkdir -p hw
"$BIN" emit-verilog "$SRC" | tee "$OUT" || exit 1
echo
note "written to $OUT"

if command -v verilator >/dev/null; then
  bar "7. VERILATOR   --lint-only"
  note "Elaborates the module: checks the ports and widths are self-consistent."
  note "Linting is not correctness. Co-simulation against stage 3 is E8.S6."
  if verilator --lint-only -Wno-fatal "$OUT"; then
    printf '\n\033[1;32mOK — %s compiled from .hc to SystemVerilog and elaborates.\033[0m\n' "$(basename "$SRC")"
  else
    echo "verilator rejected the generated module" >&2; exit 1
  fi
else
  note "verilator not on PATH, skipping the lint step"
fi
