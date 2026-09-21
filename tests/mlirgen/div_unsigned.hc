// Unsigned division does not grow: u16 / u16 is u16, and divui not divsi.
// CHECK-LABEL: func.func @dvu
// CHECK: %[[Z:.*]] = arith.cmpi eq, %arg1, %{{.*}} : i16
// CHECK: %[[SAFE:.*]] = arith.select %[[Z]], %{{.*}}, %arg1 : i16
// CHECK: %[[Q:.*]] = arith.divui %arg0, %[[SAFE]] : i16
// CHECK: arith.select %[[Z]], %{{.*}}, %[[Q]] : i16
u16 dvu(u16 a, u16 b) { return a / b; }
