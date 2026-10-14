// A variable assigned in the body but never read afterwards is carried anyway.
// MLIRGen does not try to work out which iter_args matter: `canonicalize`
// deletes the dead one in E5, which is the right place for that decision and
// keeps this stage free of a liveness analysis it would get wrong.
//
// CHECK-LABEL: func.func @unused
// CHECK: scf.for {{.*}}iter_args({{.*}}) -> (i32, i32)
u32 unused() {
  u32 acc = 0;
  u32 junk = 0;
  for (u4 i = 0; i < 4; i = i + 1) {
    acc = u32(acc + 1);
    junk = u32(junk + 2);
  }
  return acc;
}
