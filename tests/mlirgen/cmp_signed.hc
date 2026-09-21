// The signedness dropped by typeOf() reappears here, in the predicate.
// CHECK-LABEL: func.func @gts
// CHECK: arith.cmpi sgt, %arg0, %arg1 : i16
u1 gts(i16 a, i16 b) { return a > b; }
