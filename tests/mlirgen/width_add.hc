// i16 + i16 is i17: both operands extend to the result width, then add.
// CHECK-LABEL: func.func @add
// CHECK: %[[A:.*]] = arith.extsi %arg0 : i16 to i17
// CHECK: %[[B:.*]] = arith.extsi %arg1 : i16 to i17
// CHECK: arith.addi %[[A]], %[[B]] : i17
i17 add(i16 a, i16 b) { return a + b; }
