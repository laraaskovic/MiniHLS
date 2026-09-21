// `c ? a : b` is max(wa,wb) with the arms' signedness, so the narrow arm is
// extended. Still a mux, not a branch: both arms are computed.
// CHECK-LABEL: func.func @pick
// CHECK: %[[A:.*]] = arith.extsi %arg1 : i8 to i16
// CHECK: arith.select %arg0, %[[A]], %arg2 : i16
i16 pick(u1 c, i8 a, i16 b) { return c ? a : b; }
