// An over-wide unsigned right shift is 0, so this one does need the select.
// CHECK-LABEL: func.func @shru
// CHECK: %[[OOB:.*]] = arith.cmpi uge, %arg1, %{{.*}} : i4
// CHECK: %[[S:.*]] = arith.shrui %arg0, %{{.*}} : i8
// CHECK: arith.select %[[OOB]], %{{.*}}, %[[S]] : i8
u8 shru(u8 a, u4 n) { return a >> n; }
