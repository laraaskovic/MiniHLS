// Remainder takes the NARROWER width, because |a % b| < |b|. So the result
// is narrower than an operand — the work happens at max(wa,wb) and the
// answer is truncated afterwards. Truncating the divisor first would turn
// u16(257) into u8(1) and change the answer.
// CHECK-LABEL: func.func @rm
// CHECK: %[[B:.*]] = arith.extui %arg1 : i8 to i16
// CHECK: %[[Z:.*]] = arith.cmpi eq, %[[B]], %{{.*}} : i16
// CHECK: arith.remui %arg0, %{{.*}} : i16
// CHECK: arith.trunci %{{.*}} : i16 to i8
u8 rm(u16 a, u8 b) { return a % b; }
