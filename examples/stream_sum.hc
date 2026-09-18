// Sums 256 bytes arriving on a stream and writes the total out.
//
// The stream parameters become ready/valid ports, so this is the example that
// has to survive random backpressure in milestone 7.
void stream_sum(stream<u8> input, stream<u32> output) {
  u32 total = 0;
  for (u9 i = 0; i < 256; i = i + 1) {
    #pragma pipeline II=1
    u8 byte = read(input);
    total = total + byte;
  }
  write(output, total);
}
