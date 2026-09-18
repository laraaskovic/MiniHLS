// Largest of three values, written with the conditional operator.
//
// Each `?:` is a multiplexer, and the two of them chain: the critical path is
// two comparators and two muxes deep, which is the kind of chain the scheduler
// decides whether to fit in one cycle or split across two.
i16 max3(i16 a, i16 b, i16 c) {
  i16 largest = a > b ? a : b;
  return largest > c ? largest : c;
}
