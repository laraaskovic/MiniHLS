// The symbols either branch assigns become the scf.if's results — this set
// is exactly the phi nodes a textbook SSA algorithm would insert here.
// CHECK-LABEL: func.func @two
// CHECK: %[[R:.*]] = scf.if %arg0 -> (i16) {
// CHECK:   scf.yield %arg2 : i16
// CHECK: } else {
// CHECK:   scf.yield %arg1 : i16
// CHECK: }
// CHECK: return %[[R]] : i16
i16 two(u1 c, i16 a, i16 b) {
  i16 m = a;
  if (c) { m = b; } else { m = a; }
  return m;
}
