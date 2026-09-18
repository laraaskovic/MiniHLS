// An 8-tap symmetric FIR filter.
//
// This is the example the latency-versus-area curve in milestone 6 is built
// from: with one multiplier it takes eight cycles, with four it takes two, and
// the scheduler decides which by how many multipliers binding is allowed to use.
i32 fir(i16 sample, i16 history[8]) {
  const i8 coeffs[8] = { 3, -7, 12, 31, 31, 12, -7, 3 };
  #pragma partition coeffs

  // Shift the history window. Fully unrolled, so it becomes wiring rather than
  // a sequence of memory operations.
  for (u4 i = 7; i > 0; i = i - 1) {
    #pragma unroll
    history[i] = history[i - 1];
  }
  history[0] = sample;

  i32 acc = 0;
  for (u4 i = 0; i < 8; i = i + 1) {
    #pragma pipeline II=1
    acc = acc + history[i] * coeffs[i];
  }
  return acc;
}
