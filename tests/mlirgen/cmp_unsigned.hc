// Same two wires, different comparator.
// CHECK-LABEL: func.func @gtu
// CHECK: arith.cmpi ugt, %arg0, %arg1 : i16
u1 gtu(u16 a, u16 b) { return a > b; }
