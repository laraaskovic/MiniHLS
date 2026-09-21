// A comparison's operands meet at max(wa,wb) — NOT at the result width,
// which is 1. Coercing to the result width would truncate both operands to a
// single bit, and max3 would still pass every one of its tests.
// CHECK-LABEL: func.func @lt
// CHECK: %[[A:.*]] = arith.extsi %arg0 : i8 to i16
// CHECK: arith.cmpi slt, %[[A]], %arg1 : i16
u1 lt(i8 a, i16 b) { return a < b; }
