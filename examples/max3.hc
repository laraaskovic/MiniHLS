// examples/max3.hc
//
// The largest of three signed values. Straight-line code: two comparators
// and two multiplexers, no loops, no memory, no state.
i16 max3(i16 a, i16 b, i16 c) {
  i16 largest = a > b ? a : b;
  return largest > c ? largest : c;
}
