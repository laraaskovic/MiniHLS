// Narrowing keeps the low bits.
// CHECK-LABEL: func.func @narrow
// CHECK: arith.trunci %arg0 : i16 to i8
u8 narrow(u16 a) { return u8(a); }
