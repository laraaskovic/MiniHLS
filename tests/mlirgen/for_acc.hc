// A loop-carried accumulator becomes an iter_args. The three bindings are the
// three terminals of a register: %acc_init is the reset value, the region
// argument is Q, and what scf.yield carries is D.
//
// CHECK-LABEL: func.func @sum8
// CHECK: %[[Z:.*]] = arith.constant 0 : i32
// CHECK: %[[T:.*]] = scf.for %{{.*}} = %{{.*}} to %{{.*}} step %{{.*}} iter_args(%[[A:.*]] = %[[Z]]) -> (i32)
// CHECK: arith.addi
// CHECK: scf.yield
// CHECK: return %[[T]] : i32
u32 sum8() {
  u32 acc = 0;
  for (u4 i = 0; i < 8; i = i + 1) {
    acc = u32(acc + i);
  }
  return acc;
}
