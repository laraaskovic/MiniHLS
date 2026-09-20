// examples/stream_sum.hc
//
// Read eight values from a stream, write the running total back out after
// each one, and return the final total.
//
// The only example with streams, so the only one whose generated module has
// valid/ready handshakes on its ports — and therefore the one that has to
// survive a producer and a consumer that stall at random (E9.S5).
//
// `acc + v` is i33, hence the cast, same as in dot.
i32 stream_sum(in stream<i16> s, out stream<i32> r) {
  i32 acc = 0;
  for (u4 i = 0; i < 8; i = i + 1) {
    i16 v = read(s);
    acc = i32(acc + v);
    write(r, acc);
  }
  return acc;
}
