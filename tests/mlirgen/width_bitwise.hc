// Bitwise operators take max(wa,wb); only the narrow operand is extended.
// CHECK-LABEL: func.func @orr
// CHECK: %[[A:.*]] = arith.extui %arg0 : i8 to i16
// CHECK: arith.ori %[[A]], %arg1 : i16
u16 orr(u8 a, u16 b) { return a | b; }
