// An over-wide signed right shift is defined as the sign of the operand —
// 0 or -1 — which is exactly shrsi by width-1. So this one CLAMPS the amount
// and needs no correcting select afterwards.
// CHECK-LABEL: func.func @shrs
// CHECK: %[[OOB:.*]] = arith.cmpi uge, %arg1, %{{.*}} : i4
// CHECK: %[[C7:.*]] = arith.constant 7 : i4
// CHECK: %[[CL:.*]] = arith.select %[[OOB]], %[[C7]], %arg1 : i4
// CHECK: %[[BY:.*]] = arith.extui %[[CL]] : i4 to i8
// CHECK: arith.shrsi %arg0, %[[BY]] : i8
// CHECK-NOT: arith.select
i8 shrs(i8 a, u4 n) { return a >> n; }
