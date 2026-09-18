// Counts the set bits of a 16-bit word.
//
// Fully unrolled, this is sixteen one-bit adds -- a tree the scheduler can chain
// almost entirely into a single cycle, which makes it a good check that operator
// chaining under the clock period actually happens.
u5 popcount(u16 value) {
  u5 count = 0;
  for (u5 i = 0; i < 16; i = i + 1) {
    #pragma unroll
    count = count + ((value >> i) & 1);
  }
  return count;
}
