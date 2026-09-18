#!/usr/bin/env bash
#
# Report Yosys cell counts for the hand-written adder at several widths.
#
# This is the seed of the operator area library that milestone 5 needs: the
# scheduler has to know what an operator costs at a given width, and the honest
# way to find out is to synthesize it.
#
# Environment overrides:
#   WIDTHS  space-separated operand widths to synthesize (default: 4 8 16 32)

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
widths="${WIDTHS:-4 8 16 32}"

# Prefer a native Yosys, but accept the WebAssembly build from PyPI, which
# installs without root and is the easier option on locked-down machines.
if command -v yosys >/dev/null 2>&1; then
  yosys_bin=yosys
elif command -v yowasp-yosys >/dev/null 2>&1; then
  yosys_bin=yowasp-yosys
else
  echo "==> area: yosys not found, skipping cell-count report"
  echo "    apt: sudo apt-get install -y yosys"
  echo "    pip: pip install yowasp-yosys   (no root required)"
  exit 0
fi

echo "==> area: minihls_adder cell counts via ${yosys_bin}"

for width in ${widths}; do
  echo
  echo "--- WIDTH=${width} ---"
  # -Q and -T drop the banner and footer. Plain -q would also swallow the `stat`
  # report, which is the only output this script wants.
  #
  # techmap lowers the generic $add cell to gates, which is what makes the
  # counts comparable across widths instead of always reporting one adder.
  "${yosys_bin}" -Q -T -p "
    read_verilog -sv ${repo_root}/hw/minihls_adder.sv
    hierarchy -top minihls_adder -chparam WIDTH ${width}
    proc
    opt
    techmap
    opt
    stat
  " | awk '/Printing statistics/ { found = 1; next } found'
done
