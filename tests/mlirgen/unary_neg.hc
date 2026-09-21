// -a is i(wa+1). Widen FIRST, then subtract from zero, so -i8(-128) can be
// i9(128) instead of wrapping back to itself.
// CHECK-LABEL: func.func @negate
// CHECK: %[[A:.*]] = arith.extsi %arg0 : i8 to i9
// CHECK: %[[Z:.*]] = arith.constant 0 : i9
// CHECK: arith.subi %[[Z]], %[[A]] : i9
i9 negate(i8 a) { return -a; }
