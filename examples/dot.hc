// A 64-tap dot product.
//
// The canonical high-level synthesis example: a loop-carried accumulation whose
// achievable initiation interval is set by the latency of the adder feeding back
// into itself.
i32 dot(i16 a[64], i16 b[64]) {
  i32 acc = 0;
  for (u7 i = 0; i < 64; i = i + 1) {
    #pragma pipeline II=1
    acc = acc + a[i] * b[i];
  }
  return acc;
}
