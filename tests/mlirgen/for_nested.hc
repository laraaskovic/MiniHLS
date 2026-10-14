// collectAssigned recurses through the inner `for`, so `acc` is carried by
// BOTH loops: the inner scf.for yields it into the outer body, which yields it
// onward. Each emitFor binds and restores its own carried set, so the nesting
// needs no extra machinery.
//
// CHECK-LABEL: func.func @nest
// CHECK: scf.for {{.*}}iter_args({{.*}}) -> (i32)
// CHECK: scf.for {{.*}}iter_args({{.*}}) -> (i32)
// CHECK: scf.yield
// CHECK: scf.yield
u32 nest() {
  u32 acc = 0;
  for (u4 i = 0; i < 4; i = i + 1) {
    for (u4 j = 0; j < 4; j = j + 1) {
      acc = u32(acc + 1);
    }
  }
  return acc;
}
