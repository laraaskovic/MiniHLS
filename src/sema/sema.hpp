#pragma once

#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"
#include "sema/semantics.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Semantic analysis: name resolution, the width rules, and the checks that make
// a program synthesizable.
//
// On success every expression in the AST carries its type (ast::Expr::width and
// is_signed), every name its symbol, and every loop a LoopInfo with its exact
// trip count. Everything downstream relies on those annotations instead of
// recomputing them.
namespace minihls::sema {

enum class SymbolKind { Scalar, Array, Stream };

enum class StreamDirection { Unused, Input, Output };

struct Symbol {
  std::string name;
  SymbolKind kind = SymbolKind::Scalar;
  // Scalar: its type. Array and Stream: the element type.
  IntType type;
  SourceRange range;

  bool is_param = false;
  // Position in the parameter list, or -1 for a local.
  int param_index = -1;

  // Arrays.
  std::uint64_t array_size = 0;
  bool is_const = false;
  // Initial contents of a local array, one entry per element, zero-filled past
  // the end of the initializer list. Empty for parameters.
  std::vector<std::uint64_t> initial;
  // Set by `#pragma partition`: the array becomes registers with combinational
  // reads instead of a memory with a one-cycle read latency.
  bool partitioned = false;

  // Streams: inferred from whether the body reads or writes it.
  StreamDirection direction = StreamDirection::Unused;
};

struct LoopInfo {
  int induction = -1;
  // Exact number of times the body executes, found by running the loop header
  // at compile time.
  std::uint64_t trip_count = 0;
  // Induction variable value on each iteration, then its value after the loop.
  // Recorded only for loops that are unrolled completely, where lowering
  // substitutes them as constants.
  std::vector<std::uint64_t> induction_values;
  std::uint64_t final_value = 0;

  // Unroll: 0 means not unrolled, otherwise the number of body copies per
  // iteration of the remaining loop (trip_count means completely).
  std::uint64_t unroll_factor = 0;
  // Pipeline: requested initiation interval, 0 when not requested.
  std::uint64_t pipeline_ii = 0;
  SourceRange range;
};

struct FunctionInfo {
  const ast::Function* function = nullptr;
  std::vector<Symbol> symbols;
  // Symbol index of each parameter, in declaration order.
  std::vector<int> params;
  bool returns_void = false;
  IntType return_type;
  std::unordered_map<const ast::Stmt*, LoopInfo> loops;
  // Places where assignment silently drops bits. Reported on request, since
  // truncation is legal and usually intended.
  std::vector<Diagnostic> truncations;
};

struct CheckedProgram {
  std::vector<FunctionInfo> functions;

  const FunctionInfo* find(std::string_view name) const;
};

// Checks a parsed program, annotating the AST in place. Returns false if any
// error was reported, in which case `out` must not be used.
bool check(ast::Program& program, DiagnosticEngine& diagnostics, CheckedProgram& out);

// The most iterations a loop may have. Its header is run at compile time, so an
// unbounded loop has to be caught rather than hang the compiler.
inline constexpr std::uint64_t kMaxTripCount = 1u << 20;

}  // namespace minihls::sema
