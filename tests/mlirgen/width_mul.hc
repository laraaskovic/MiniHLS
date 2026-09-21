// Multiplication sums the widths: i16 * i16 is i32.
// CHECK-LABEL: func.func @mul
// CHECK: %[[A:.*]] = arith.extsi %arg0 : i16 to i32
// CHECK: %[[B:.*]] = arith.extsi %arg1 : i16 to i32
// CHECK: arith.muli %[[A]], %[[B]] : i32
i32 mul(i16 a, i16 b) { return a * b; }
