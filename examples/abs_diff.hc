// examples/abs_diff.hc
//
// |a - b| for two signed 16-bit values.
//
// Every line here is a width rule. `a - b` is i17, because subtraction is
// always signed and always grows by one bit: 32767 - (-32768) is 65535,
// which no 16-bit type holds. Negating an i17 gives an i18 for the same
// reason. The result then fits u16 exactly — 0 .. 65535 — so the cast is
// lossless, but it is still written down, because it changes signedness.
u16 abs_diff(i16 a, i16 b) {
  i17 d = a - b;
  i18 m = d < 0 ? -d : d;
  return u16(m);
}
