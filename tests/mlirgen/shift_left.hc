// Shifts keep the LEFT operand's width and are the one place the language
// drops bits without a cast. The out-of-range test runs on the ORIGINAL
// amount, before it is coerced, because truncating an over-wide amount would
// turn it into a shift by zero.
//
// The bound prints as `-8 : i4` because MLIR renders i4 signed; the bit
// pattern is 1000, which is 8 to the unsigned `uge` that reads it.
// CHECK-LABEL: func.func @shl
// CHECK: %[[LIM:.*]] = arith.constant -8 : i4
// CHECK: %[[OOB:.*]] = arith.cmpi uge, %arg1, %[[LIM]] : i4
// CHECK: %[[IN:.*]] = arith.select %[[OOB]], %{{.*}}, %arg1 : i4
// CHECK: %[[BY:.*]] = arith.extui %[[IN]] : i4 to i8
// CHECK: %[[S:.*]] = arith.shli %arg0, %[[BY]] : i8
// CHECK: arith.select %[[OOB]], %{{.*}}, %[[S]] : i8
u8 shl(u8 a, u4 n) { return a << n; }
