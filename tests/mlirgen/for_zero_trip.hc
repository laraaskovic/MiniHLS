// A loop that runs zero times emits no scf.for at all. Every symbol the body
// would have assigned keeps the value it already had — the same thing the
// interpreter does, which matters because E4.S8 compares the two.
//
// CHECK-LABEL: func.func @never
// CHECK-NOT: scf.for
// CHECK: return
u32 never() {
  u32 acc = 7;
  for (u4 i = 0; i < 0; i = i + 1) {
    acc = u32(acc + 1);
  }
  return acc;
}
