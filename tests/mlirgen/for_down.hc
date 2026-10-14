// `i = i - 1` against a `>` test. Every header is normalised to
// 0 .. tripCount step 1 in `index`, and the real induction variable is rebuilt
// as init - k*step — so down-counting needs no lowering of its own, and the
// loop condition is never re-tested in hardware.
//
// CHECK-LABEL: func.func @down
// CHECK: %[[K:.*]] = arith.index_cast %{{.*}} : index to i4
// CHECK: %[[D:.*]] = arith.muli %[[K]], %{{.*}} : i4
// CHECK: arith.subi %{{.*}}, %[[D]] : i4
u32 down() {
  u32 acc = 0;
  for (u4 i = 8; i > 0; i = i - 1) {
    acc = u32(acc + i);
  }
  return acc;
}
