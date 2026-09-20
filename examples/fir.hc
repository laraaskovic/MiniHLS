// examples/fir.hc
//
// One output sample of an 8-tap FIR filter.
//
// Structurally this is dot with one vector frozen into a ROM: the taps are
// file-scope const, so they become a constant memory rather than a second
// read port, and the eight multiplications all share one operand source.
// That is what makes it the example for resource sharing in E10 — the same
// program built with 1, 2 and 4 multipliers.
//
// The taps sum to zero, which gives the test file a property worth
// checking: any constant input must produce exactly 0.
const i16 taps[8] = { 1, -2, 3, -4, 4, -3, 2, -1 };

i32 fir(i16 x[8]) {
  i32 acc = 0;
  for (u4 i = 0; i < 8; i = i + 1) {
    acc = i32(acc + taps[i] * x[i]);
  }
  return acc;
}
