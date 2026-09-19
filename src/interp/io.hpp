#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Inputs and observable outputs of one call to a top-level function.
//
// The same structures feed the AST interpreter, the MLIR execution check, and
// the generated Verilator testbench, which is what makes their results directly
// comparable. Every vector is indexed by parameter position; entries for
// parameters of another kind are left empty.
namespace minihls::interp {

struct Inputs {
  // Scalar parameters: the value's bits.
  std::vector<std::uint64_t> scalars;
  // Array parameters: initial contents, one entry per element.
  std::vector<std::vector<std::uint64_t>> arrays;
  // Input streams: the data available to read(), in order.
  std::vector<std::vector<std::uint64_t>> streams;
};

struct Outputs {
  // Empty on success; otherwise what went wrong (an out-of-bounds index, a read
  // past the end of a stream's input, a step limit).
  std::string error;
  std::optional<std::uint64_t> return_value;
  // Array parameters: final contents.
  std::vector<std::vector<std::uint64_t>> arrays;
  // Output streams: everything written, in order.
  std::vector<std::vector<std::uint64_t>> streams;
  // Input streams: how many elements were consumed.
  std::vector<std::uint64_t> consumed;

  bool ok() const { return error.empty(); }
};

// Guards against runaway execution in the interpreters. Every loop has a
// compile-time trip count, so hitting this means an interpreter bug.
inline constexpr std::uint64_t kMaxSteps = 200'000'000;

// Describes the first difference between two results, or returns an empty
// string when they agree.
std::string compare(const Outputs& expected, const Outputs& actual);

}  // namespace minihls::interp
