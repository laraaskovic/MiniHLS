#pragma once

// Shared fixture for the MLIR test suites: parse a program, check it, generate
// MLIR, and compare the result of running that MLIR against the AST
// interpreter. MLIRGen's tests use it to check the IR it produces; the
// optimization tests use it to check that every pass preserves behaviour.

#include "../unit/sema/checked.hpp"
#include "interp/ast_interp.hpp"
#include "interp/io.hpp"
#include "interp/random_inputs.hpp"
#include "mlirgen/execution.hpp"
#include "mlirgen/mlirgen.hpp"

#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace minihls::testing {

inline std::string read_example(const std::string& name) {
  std::ifstream stream(std::filesystem::path(MINIHLS_EXAMPLES_DIR) / name);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

// A set of inputs and the answer the golden interpreter gives for it.
struct Vector {
  interp::Inputs inputs;
  interp::Outputs expected;
};

class Generated {
 public:
  explicit Generated(std::string source) : checked_(std::move(source)) {
    if (!checked_.ok()) return;
    module_ = mlirgen::generate(context_, checked_.function(), checked_.file(),
                                checked_.mutable_diagnostics());
  }

  bool ok() const { return checked_.ok() && module_; }
  std::string messages() const { return checked_.messages(); }
  const sema::FunctionInfo& function() const { return checked_.function(); }
  mlir::ModuleOp module() { return *module_; }

  std::string text() {
    std::string out;
    llvm::raw_string_ostream stream(out);
    module_->print(stream);
    return out;
  }

  // Input sets the interpreter accepts, with its answers. Sets it rejects --
  // an out-of-bounds index, say -- are dropped, since there is nothing for the
  // compiled code to agree with.
  std::vector<Vector> sample(int count, std::uint64_t seed) {
    std::mt19937_64 rng(seed);
    std::vector<Vector> vectors;
    for (int v = 0; v < count; ++v) {
      auto inputs = interp::random_inputs(function(), rng);
      auto expected = interp::run_ast(function(), inputs);
      if (!expected.ok()) continue;
      vectors.push_back({std::move(inputs), std::move(expected)});
    }
    return vectors;
  }

  // Compiles the module as it stands and runs every vector through it. Returns
  // the first disagreement, or an empty string if all of them match. `where`
  // names the stage, so a failure says which pass broke the program.
  std::string disagreement(const std::vector<Vector>& vectors, std::string_view where) {
    std::string error;
    auto executable = mlirgen::Executable::create(module(), function(), error);
    if (!executable) return std::string(where) + ": could not compile: " + error;
    for (std::size_t v = 0; v < vectors.size(); ++v) {
      const std::string difference =
          interp::compare(vectors[v].expected, executable->run(vectors[v].inputs));
      if (!difference.empty()) {
        return std::string(where) + ": vector " + std::to_string(v) + ": " + difference;
      }
    }
    return {};
  }

  // Generates vectors and compares in one step. Returns how many were compared.
  int compare_on_random_inputs(int count, std::uint64_t seed) {
    const auto vectors = sample(count, seed);
    const std::string difference = disagreement(vectors, "mlirgen");
    EXPECT_EQ(difference, "") << text();
    return difference.empty() ? static_cast<int>(vectors.size()) : 0;
  }

 private:
  Checked checked_;
  mlir::MLIRContext context_;
  mlir::OwningOpRef<mlir::ModuleOp> module_;
};

}  // namespace minihls::testing
