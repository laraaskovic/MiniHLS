// examples/dot.hc written by hand in MLIR, following the language's width
// rules exactly -- which is also what MLIRGen will do in milestone P3.
//
//   i32 dot(i16 a[64], i16 b[64]) {
//     i32 acc = 0;
//     for (u7 i = 0; i < 64; i = i + 1) {
//       acc = acc + a[i] * b[i];
//     }
//     return acc;
//   }
//
//   mlir-opt docs/mlir-by-hand/dot.mlir --canonicalize --cse --mlir-print-ir-after-all

func.func @dot(%a: memref<64xi16>, %b: memref<64xi16>) -> i32 {
  // `index` is MLIR's type for loop counters and memory addresses.
  %c0 = arith.constant 0 : index
  %c1 = arith.constant 1 : index
  %c64 = arith.constant 64 : index
  %zero = arith.constant 0 : i32

  // The loop runs 64 times. `acc` is carried from one iteration to the next as
  // an iter_arg: it enters as %zero, each iteration's `scf.yield` supplies the
  // next value, and the loop's result (%sum) is the value after the last
  // iteration. This is MLIR's structured form of a phi node.
  %sum = scf.for %i = %c0 to %c64 step %c1 iter_args(%acc = %zero) -> (i32) {
    %x = memref.load %a[%i] : memref<64xi16>
    %y = memref.load %b[%i] : memref<64xi16>

    // a[i] * b[i]: i16 * i16 is i32, so both operands are sign-extended to 32
    // bits before multiplying. The product cannot overflow.
    %x32 = arith.extsi %x : i16 to i32
    %y32 = arith.extsi %y : i16 to i32
    %product = arith.muli %x32, %y32 : i32

    // acc + product: i32 + i32 is i33, one bit wider so the sum cannot
    // overflow ...
    %acc33 = arith.extsi %acc : i32 to i33
    %product33 = arith.extsi %product : i32 to i33
    %wide = arith.addi %acc33, %product33 : i33

    // ... and assigning it to the i32 `acc` keeps the low 32 bits.
    %next = arith.trunci %wide : i33 to i32
    scf.yield %next : i32
  }
  return %sum : i32
}
