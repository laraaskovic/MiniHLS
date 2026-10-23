// tests/cosim/tb_max3.cpp — co-simulate the generated max3 against its vectors.
//
//   i16 max3(i16 a, i16 b, i16 c)
//
// Everything that is not specific to max3's ports lives in cosim.hpp.
#include "Vmax3.h"
#include "cosim.hpp"

// Verilator types a port by its WIDTH, not by its signedness: 1-8 bits is
// uint8_t, 9-16 is uint16_t, 17-32 is uint32_t. Assigning a negative value
// truncates to two's complement, which is what the wires hold, so the inputs
// need no special handling.
static void bind(Vmax3& dut, const std::vector<long long>& in) {
  dut.a = static_cast<uint16_t>(in.at(0));
  dut.b = static_cast<uint16_t>(in.at(1));
  dut.c = static_cast<uint16_t>(in.at(2));
}

// Reading back DOES need it. `result` arrives as a uint16_t, so -2 comes out as
// 65534 and comparing it against the expected -2 fails. The int16_t cast is the
// last restatement of a fact that has now been stored in four different places:
// the type in .hc (`i16`), the operation in MLIR (`cmpi sgt`), the expression in
// Verilog (`$signed(a) > $signed(b)`), and here, the C++ type.
static long long read(Vmax3& dut) { return static_cast<int16_t>(dut.result); }

int main(int argc, char** argv) {
  return minihls::cosim::run<Vmax3>(argc, argv, bind, read);
}
