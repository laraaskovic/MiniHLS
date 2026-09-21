// A symbol assigned in only ONE branch still needs a result: the branch that
// does not touch it yields the value it held before the `if`. That falls out
// for free, because each branch starts from the bindings in force before it.
// CHECK-LABEL: func.func @one
// CHECK: %[[R:.*]] = scf.if %arg0 -> (i16) {
// CHECK:   scf.yield %arg2 : i16
// CHECK: } else {
// CHECK:   scf.yield %arg1 : i16
// CHECK: }
// CHECK: return %[[R]] : i16
i16 one(u1 c, i16 a, i16 b) {
  i16 m = a;
  if (c) { m = b; }
  return m;
}
