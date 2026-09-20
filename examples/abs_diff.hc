// examples/abs_diff.hc
//
// |a - b| for two signed 16-bit values.
//
// Every line here is a width rule. `a - b` is i17, because subtraction is
// always signed and always grows by one bit: 32767 - (-32768) is 65535,
// which no 16-bit type holds. Negating an i17 gives an i18 for the same
// reason. The result then fits u16 exactly — 0 .. 65535 — so the cast is
// lossless, but it is still written down, because it changes signedness.
//
// The sign test is an `if`/`else`, not a `?:`, because this is the suite's
// branch: it has to become an `scf.if` with a result in E4.S5 and a
// two-way next state in E8.S2. `?:` is covered by max3.
u16 abs_diff(i16 a, i16 b) {
  i17 d = a - b;
  i18 m = 0;
  if (d < 0) {
    m = -d;
  } else {
    m = d;
  }
  return u16(m);
}
