#!/usr/bin/env bash
#
# Milestone P1: run the hand-written MLIR through MLIR's tools.
#
#   bash docs/mlir-by-hand/run.sh
#
# 1. Verifies and canonicalizes max3.mlir and dot.mlir, printing the result.
# 2. Lowers dot.mlir plus its test driver to the LLVM dialect, step by step,
#    and runs it with mlir-runner. The expected output is 87360.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
prefix="${MINIHLS_CIRCT_PREFIX:-$HOME/.local/circt}"
mlir_opt="${prefix}/bin/mlir-opt"
mlir_runner="${prefix}/bin/mlir-runner"

echo "==> max3.mlir after --canonicalize"
"${mlir_opt}" "${here}/max3.mlir" --canonicalize

echo
echo "==> dot.mlir after --canonicalize --cse"
"${mlir_opt}" "${here}/dot.mlir" --canonicalize --cse

echo
echo "==> dot.mlir lowered to LLVM and run"
# Each pass lowers one dialect: structured loops to plain branches, then every
# remaining dialect into `llvm`. reconcile-unrealized-casts removes the
# temporary casts the individual conversions leave between each other.
cat "${here}/dot.mlir" "${here}/dot_main.mlir" |
  "${mlir_opt}" \
    --convert-scf-to-cf \
    --convert-arith-to-llvm \
    --finalize-memref-to-llvm \
    --convert-func-to-llvm \
    --convert-cf-to-llvm \
    --reconcile-unrealized-casts |
  "${mlir_runner}" -e main -entry-point-result=void \
    -shared-libs="${prefix}/lib/libmlir_c_runner_utils.so"
