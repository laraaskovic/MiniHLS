// examples/popcount.hc
//
// Number of set bits in a 16-bit word. The result is 0 .. 16, so u5.
//
// The loop counter is u5, not u4: the range rule requires the induction
// variable's type to hold the value that ends the loop, and 16 does not fit
// in u4. `x >> i` needs an unsigned shift amount, which u5 also is.
u5 popcount(u16 x) {
  u5 count = 0;
  for (u5 i = 0; i < 16; i = i + 1) {
    u1 bit = u1((x >> i) & 1);
    count = u5(count + bit);
  }
  return count;
}
