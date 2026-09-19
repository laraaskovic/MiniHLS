// examples/max3.hc written by hand in MLIR's software-level dialects: the form
// MLIRGen will produce in milestone P3. Compare it with hw/max3.mlir, the same
// function in CIRCT's hardware dialects -- getting from this file to that one
// is what the rest of the compiler does.
//
//   mlir-opt docs/mlir-by-hand/max3.mlir --canonicalize

// i16 max3(i16 a, i16 b, i16 c)
func.func @max3(%a: i16, %b: i16, %c: i16) -> i16 {
  // i16 largest = a > b ? a : b;
  // `sgt` is signed greater-than: MLIR integers are signless, so the
  // comparison says how to read the bits.
  %a_gt_b = arith.cmpi sgt, %a, %b : i16
  %largest = arith.select %a_gt_b, %a, %b : i16

  // return largest > c ? largest : c;
  %largest_gt_c = arith.cmpi sgt, %largest, %c : i16
  %result = arith.select %largest_gt_c, %largest, %c : i16
  return %result : i16
}
