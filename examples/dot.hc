// examples/dot.hc
//
// Inner product of two 8-element vectors: a loop, two array reads per
// iteration, and one loop-carried accumulator.
//
// `x[i] * y[i]` is i32 (16 + 16). Adding it to acc wants i33, one bit more
// than acc can hold, so the program states with a cast that the sum fits —
// and the compiler gets to build a 32-bit adder instead of a 33-bit one.
// Where a case in dot.tests does not fit, the cast truncates, and all three
// of the interpreter, the MLIR and the RTL must truncate identically.
//
// II=1 is the request E9 has to answer, and it may well not be met: `acc`
// is a distance-1 recurrence, and two reads per iteration against a
// single-port memory push it further. Reporting requested against
// achieved, and which of the two bound it, is E9.S2.
i32 dot(i16 x[8], i16 y[8]) {
  i32 acc = 0;
#pragma pipeline II=1
  for (u4 i = 0; i < 8; i = i + 1) {
    acc = i32(acc + x[i] * y[i]);
  }
  return acc;
}
