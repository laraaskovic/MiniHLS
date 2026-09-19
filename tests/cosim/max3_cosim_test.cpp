// Co-simulation of hw/max3.mlir -- max3 written by hand in CIRCT's hardware
// dialects and exported to Verilog by CIRCT -- against the reference
// interpreter running examples/max3.hc.
//
// This is the end-to-end check for milestone P0, and the pattern every
// generated design follows from P7 on: the same inputs go to the Verilated
// hardware and to the AST interpreter, and the outputs must match bit for bit.

#include "Vmax3.h"
#include "frontend/diagnostics.hpp"
#include "frontend/parser.hpp"
#include "frontend/source.hpp"
#include "interp/ast_interp.hpp"
#include "sema/sema.hpp"
#include "support/bits.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>

namespace {

class Max3Cosim : public ::testing::Test {
 protected:
  void SetUp() override {
    const auto path = std::filesystem::path(MINIHLS_EXAMPLES_DIR) / "max3.hc";
    std::ifstream stream(path);
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    file_ = std::make_unique<minihls::SourceFile>(path.string(), buffer.str());
    diagnostics_ = std::make_unique<minihls::DiagnosticEngine>(*file_);
    program_ = minihls::parse(*file_, *diagnostics_);
    ASSERT_TRUE(minihls::sema::check(*program_, *diagnostics_, checked_))
        << diagnostics_->format_all();
    dut_ = std::make_unique<Vmax3>();
  }

  void TearDown() override {
    if (dut_) dut_->final();
  }

  // Runs both models on one input and returns {hardware, interpreter}.
  std::pair<std::uint64_t, std::uint64_t> evaluate(std::int64_t a, std::int64_t b,
                                                   std::int64_t c) {
    const auto bits = [](std::int64_t v) {
      return minihls::truncate(static_cast<std::uint64_t>(v), 16);
    };
    dut_->a = static_cast<std::uint16_t>(bits(a));
    dut_->b = static_cast<std::uint16_t>(bits(b));
    dut_->c = static_cast<std::uint16_t>(bits(c));
    dut_->eval();

    minihls::interp::Inputs inputs;
    inputs.scalars = {bits(a), bits(b), bits(c)};
    inputs.arrays.assign(3, {});
    inputs.streams.assign(3, {});
    const auto outputs = minihls::interp::run_ast(checked_.functions.front(), inputs);
    EXPECT_TRUE(outputs.ok()) << outputs.error;
    return {dut_->result, outputs.return_value.value_or(~std::uint64_t{0})};
  }

  std::unique_ptr<minihls::SourceFile> file_;
  std::unique_ptr<minihls::DiagnosticEngine> diagnostics_;
  std::unique_ptr<minihls::ast::Program> program_;
  minihls::sema::CheckedProgram checked_;
  std::unique_ptr<Vmax3> dut_;
};

TEST_F(Max3Cosim, MatchesTheInterpreterOnRandomInputs) {
  // Fixed seed: a co-simulation failure is only useful if it reproduces.
  std::mt19937_64 rng(0x3AB3ULL);
  std::uniform_int_distribution<std::int64_t> value(-32768, 32767);
  for (int iteration = 0; iteration < 100000; ++iteration) {
    const std::int64_t a = value(rng);
    const std::int64_t b = value(rng);
    const std::int64_t c = value(rng);
    const auto [hardware, reference] = evaluate(a, b, c);
    ASSERT_EQ(hardware, reference) << "a=" << a << " b=" << b << " c=" << c;
  }
}

// A comparator that got signedness wrong agrees with the right one on half the
// inputs and fails at the boundary between positive and negative.
TEST_F(Max3Cosim, MatchesTheInterpreterAtSignBoundaries) {
  const std::int64_t corners[] = {-32768, -32767, -1, 0, 1, 32766, 32767};
  for (const std::int64_t a : corners) {
    for (const std::int64_t b : corners) {
      for (const std::int64_t c : corners) {
        const auto [hardware, reference] = evaluate(a, b, c);
        ASSERT_EQ(hardware, reference) << "a=" << a << " b=" << b << " c=" << c;
      }
    }
  }
}

}  // namespace
