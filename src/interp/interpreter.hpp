#pragma once
#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"
#include "sema/symbol.hpp"
#include "support/bits.hpp"
#include <optional>
#include <unordered_map>
#include <vector>

namespace minihls {

// One per parameter, in declaration order.
//   scalar param -> `scalar`;  array param -> `elements`
//   in  stream   -> `elements`, consumed in order
//   out stream   -> ignored on input; results come back in RunResult
struct ParamValue {
  Bits scalar;
  std::vector<Bits> elements;
};

struct RunResult {
  Bits returnValue;
  std::vector<std::vector<Bits>> streamOutputs;   // one per `out` stream, in order
};

class Interpreter {
public:
  explicit Interpreter(Diagnostics& diags) : diags_(diags) {}

  std::optional<RunResult> run(Program& program,
                               const std::vector<ParamValue>& inputs);

private:
  Bits eval(Expr& e);
  void exec(Stmt& s);
  void execBlock(Block& b);
  Bits readStream(const Symbol* s, Range where);

  Bits loadElem(const Symbol* s, const Bits& index);
  void storeElem(const Symbol* s, const Bits& index, Bits v);
  static Bits coerce(Bits v, Type t) { return castTo(v, t.width, t.isSigned); }

  std::unordered_map<const Symbol*, Bits> scalars_;
  std::unordered_map<const Symbol*, std::vector<Bits>> arrays_;
  std::unordered_map<const Symbol*, std::vector<Bits>> streamIn_;
  std::unordered_map<const Symbol*, size_t> streamPos_;
  std::unordered_map<const Symbol*, std::vector<Bits>> streamOut_;

  Bits returnValue_;
  bool failed_ = false;
  Diagnostics& diags_;
};

} // namespace minihls