#include <verilated.h>
#include "Vmax3.h"
#include <cstdint>
#include <cstdio>
#include <random>

static int16_t expected(int16_t a, int16_t b, int16_t c) {
  int16_t m = a > b ? a : b;
  return m > c ? m : c;
}

static int check(Vmax3* dut, int16_t a, int16_t b, int16_t c) {
  dut->a = static_cast<uint16_t>(a);
  dut->b = static_cast<uint16_t>(b);
  dut->c = static_cast<uint16_t>(c);
  dut->eval();
  int16_t got = static_cast<int16_t>(dut->result);
  int16_t want = expected(a, b, c);
  if (got != want) {
    std::printf("FAIL a=%d b=%d c=%d -> got %d, want %d\n", a, b, c, got, want);
    return 1;
  }
  return 0;
}

int main(int argc, char** argv) {
  Verilated::commandArgs(argc, argv);
  Vmax3* dut = new Vmax3;
  int failures = 0;

  // Edge values: this is where signedness bugs live.
  const int16_t edges[] = {INT16_MIN, -1, 0, 1, INT16_MAX};
  for (int16_t a : edges)
    for (int16_t b : edges)
      for (int16_t c : edges)
        failures += check(dut, a, b, c);

  std::mt19937 rng(12345);              // fixed seed: failures reproduce
  std::uniform_int_distribution<int> d(INT16_MIN, INT16_MAX);
  for (int i = 0; i < 200000; ++i)
    failures += check(dut, d(rng), d(rng), d(rng));

  dut->final();
  delete dut;
  std::printf(failures ? "FAILED: %d\n" : "OK\n", failures);
  return failures != 0;
}