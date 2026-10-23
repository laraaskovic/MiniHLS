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


if ! command -v verilator >/dev/null; then
  note "verilator not on PATH, skipping the lint and co-simulation steps"
  exit 0
fi

bar "7. VERILATOR   --lint-only"
note "Elaborates the module: checks the ports and widths are self-consistent."
note "This proves the Verilog is LEGAL. It says nothing about the answers."
verilator --lint-only -Wno-fatal "$OUT" || { echo "verilator rejected it" >&2; exit 1; }
echo "lint clean"

NAME="$(basename "${SRC%.hc}")"
TB="tests/cosim/tb_${NAME}.cpp"
if [[ ! -f "$TB" ]]; then
  bar "8. CO-SIMULATION   (not wired for $NAME)"
  note "$TB does not exist, so this example is not co-simulated yet."
  exit 0
fi

bar "8. CO-SIMULATION   the same vectors, through the hardware"
note "Stage 3 ran these vectors through the interpreter. Now they go through"
note "the generated circuit. Every answer has to match bit for bit, including"
note "the cases C leaves undefined and this language does not."

WORK="$BUILD/demo-cosim-$NAME"
mkdir -p "$WORK"
# Absolute paths: verilator runs make inside -Mdir and passes these through
# unchanged, so a path relative to the repo root resolves to nothing there.
verilator --cc --exe --build -Wno-fatal \
  -Mdir "$WORK/obj" \
  -CFLAGS "-I$PWD/tests/cosim -std=c++20 -Wall" \
  "$PWD/$OUT" "$PWD/$TB" > "$WORK/build.log" 2>&1 \
  || { echo "testbench build failed, see $WORK/build.log" >&2; tail -20 "$WORK/build.log" >&2; exit 1; }

if ! "$WORK/obj/V$NAME" "${SRC%.hc}.tests"; then
  printf '\n\033[1;31mThe generated hardware does not match the reference.\033[0m\n' >&2
  exit 1
fi

printf '\n\033[1;32mOK: %s went from .hc to SystemVerilog, and the hardware agrees with\n' "$(basename "$SRC")"
printf '    the reference interpreter on every vector.\033[0m\n'
