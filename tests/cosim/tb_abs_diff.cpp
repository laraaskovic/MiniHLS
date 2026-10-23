// tests/cosim/tb_abs_diff.cpp — co-simulate the generated abs_diff.
//
//   u16 abs_diff(i16 a, i16 b)
//
// The interesting one: signed inputs, UNSIGNED result. abs_diff(32767, -32768)
// is 65535, which no i16 holds — the reason the source declares the result u16
// and casts on the way out. So unlike max3 the result is read back WITHOUT a
// sign cast, and getting that backwards would turn 65535 into -1.
#include "Vabs_diff.h"
#include "cosim.hpp"

static void bind(Vabs_diff& dut, const std::vector<long long>& in) {
  dut.a = static_cast<uint16_t>(in.at(0));
  dut.b = static_cast<uint16_t>(in.at(1));
}

static long long read(Vabs_diff& dut) { return static_cast<uint16_t>(dut.result); }

int main(int argc, char** argv) {
  return minihls::cosim::run<Vabs_diff>(argc, argv, bind, read);
}
