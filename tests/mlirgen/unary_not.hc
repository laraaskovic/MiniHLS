// ~a is a xor all-ones, at the same width. -1 : i8 IS all ones.
// CHECK-LABEL: func.func @flip
// CHECK: %[[ONES:.*]] = arith.constant -1 : i8
// CHECK: arith.xori %arg0, %[[ONES]] : i8
u8 flip(u8 a) { return ~a; }
