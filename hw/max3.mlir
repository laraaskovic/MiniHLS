// examples/max3.hc, written by hand in CIRCT's hardware dialects.
//
// This is milestone P0's end-to-end check of the toolchain: CIRCT turns this
// into SystemVerilog, Verilator simulates it, and the result is compared with
// the reference interpreter running the original source. It is also a preview
// of what the compiler will generate: for max3, everything fits in one clock
// cycle, so the whole function is combinational logic.
//
//   hw.module    a hardware module with typed input and output ports
//   comb.icmp    a comparator; `sgt` is signed greater-than
//   comb.mux     a multiplexer, which is what `?:` becomes
//   hw.output    connects values to the output ports
//
// Regenerate the Verilog by hand with:
//   circt-opt hw/max3.mlir --export-verilog -o /dev/null

hw.module @max3(in %a : i16, in %b : i16, in %c : i16, out result : i16) {
  // i16 largest = a > b ? a : b;
  %a_gt_b = comb.icmp sgt %a, %b : i16
  %largest = comb.mux %a_gt_b, %a, %b : i16

  // return largest > c ? largest : c;
  %largest_gt_c = comb.icmp sgt %largest, %c : i16
  %result = comb.mux %largest_gt_c, %largest, %c : i16

  hw.output %result : i16
}
