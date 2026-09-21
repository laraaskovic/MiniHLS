// Signed division grows by one bit so i16(-32768) / i16(-1) has somewhere to
// go. The divisor is guarded, not the result: a zero must never reach
// arith.divsi, or the optimiser may delete the correction with it.
// CHECK-LABEL: func.func @dvs
// CHECK: %[[ZERO:.*]] = arith.constant 0 : i17
// CHECK: %[[Z:.*]] = arith.cmpi eq, %{{.*}}, %[[ZERO]] : i17
// CHECK: %[[SAFE:.*]] = arith.select %[[Z]], %{{.*}}, %{{.*}} : i17
// CHECK: %[[Q:.*]] = arith.divsi %{{.*}}, %[[SAFE]] : i17
// CHECK: arith.select %[[Z]], %[[ZERO]], %[[Q]] : i17
i17 dvs(i16 a, i16 b) { return a / b; }
