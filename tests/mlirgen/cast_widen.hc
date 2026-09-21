// Widening extends by the SOURCE's signedness.
// CHECK-LABEL: func.func @widen
// CHECK: arith.extsi %arg0 : i16 to i32
i32 widen(i16 a) { return i32(a); }
