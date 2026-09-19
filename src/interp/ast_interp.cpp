#include "interp/ast_interp.hpp"

#include "support/int128.hpp"

#include <string>
#include <vector>

namespace minihls::interp {
namespace {

using ast::Expr;
using ast::ExprKind;
using ast::Stmt;
using ast::StmtKind;
using sema::IntType;
using sema::Symbol;
using sema::SymbolKind;
using sema::TypedValue;

// Thrown to abandon execution on a runtime error; caught in run_ast().
struct RuntimeError {
  std::string message;
};

class AstInterpreter {
 public:
  AstInterpreter(const sema::FunctionInfo& function, const Inputs& inputs)
      : function_(function), inputs_(inputs) {}

  Outputs run();

 private:
  const Symbol& symbol(int index) const {
    return function_.symbols[static_cast<std::size_t>(index)];
  }

  void exec(const Stmt& stmt);
  TypedValue eval(const Expr& expr);
  std::size_t element_index(const Expr& index_expr, const Symbol& array);
  void step();

  const sema::FunctionInfo& function_;
  const Inputs& inputs_;

  std::vector<TypedValue> scalars_;
  std::vector<std::vector<std::uint64_t>> arrays_;
  // Per symbol: the read position of an input stream, or the data written to
  // an output stream.
  std::vector<std::size_t> read_position_;
  std::vector<std::vector<std::uint64_t>> written_;

  bool returned_ = false;
  std::optional<std::uint64_t> return_value_;
  std::uint64_t steps_ = 0;
};

void AstInterpreter::step() {
  if (++steps_ > kMaxSteps) throw RuntimeError{"step limit exceeded"};
}

Outputs AstInterpreter::run() {
  const std::size_t count = function_.symbols.size();
  scalars_.resize(count);
  arrays_.resize(count);
  read_position_.assign(count, 0);
  written_.resize(count);

  // Every scalar starts at zero, including locals declared without an
  // initializer; arrays start from their declared contents.
  for (std::size_t i = 0; i < count; ++i) {
    const Symbol& s = function_.symbols[i];
    scalars_[i] = {0, s.type};
    if (s.kind == SymbolKind::Array && !s.is_param) arrays_[i] = s.initial;
  }
  for (const int index : function_.params) {
    const Symbol& s = symbol(index);
    const auto position = static_cast<std::size_t>(s.param_index);
    const auto slot = static_cast<std::size_t>(index);
    if (s.kind == SymbolKind::Scalar) {
      scalars_[slot] = {truncate(inputs_.scalars.at(position), s.type.width), s.type};
    } else if (s.kind == SymbolKind::Array) {
      arrays_[slot] = inputs_.arrays.at(position);
      arrays_[slot].resize(s.array_size, 0);
      for (auto& element : arrays_[slot]) element = truncate(element, s.type.width);
    }
  }

  Outputs outputs;
  try {
    exec(*function_.function->body);
  } catch (const RuntimeError& error) {
    outputs.error = error.message;
    return outputs;
  }

  const std::size_t params = function_.params.size();
  outputs.return_value = return_value_;
  outputs.arrays.resize(params);
  outputs.streams.resize(params);
  outputs.consumed.assign(params, 0);
  for (const int index : function_.params) {
    const Symbol& s = symbol(index);
    const auto position = static_cast<std::size_t>(s.param_index);
    const auto slot = static_cast<std::size_t>(index);
    if (s.kind == SymbolKind::Array && !s.is_const) outputs.arrays[position] = arrays_[slot];
    if (s.kind == SymbolKind::Stream) {
      if (s.direction == sema::StreamDirection::Output) {
        outputs.streams[position] = written_[slot];
      } else {
        outputs.consumed[position] = read_position_[slot];
      }
    }
  }
  return outputs;
}

void AstInterpreter::exec(const Stmt& stmt) {
  if (returned_) return;
  step();
  switch (stmt.kind) {
    case StmtKind::Block:
      for (const auto& child : stmt.statements) {
        exec(*child);
        if (returned_) return;
      }
      return;

    case StmtKind::VarDecl: {
      const Symbol& s = symbol(stmt.symbol);
      if (s.kind == SymbolKind::Array) return;  // initialized on entry
      TypedValue value{0, s.type};
      if (!stmt.init.empty()) value = sema::convert(eval(*stmt.init.front()), s.type);
      scalars_[static_cast<std::size_t>(stmt.symbol)] = value;
      return;
    }

    case StmtKind::Assign: {
      const TypedValue value = eval(*stmt.value);
      const Expr& target = *stmt.target;
      if (target.kind == ExprKind::Name) {
        const Symbol& s = symbol(target.symbol);
        scalars_[static_cast<std::size_t>(target.symbol)] = sema::convert(value, s.type);
      } else {
        const Symbol& s = symbol(target.lhs->symbol);
        const std::size_t index = element_index(*target.rhs, s);
        arrays_[static_cast<std::size_t>(target.lhs->symbol)][index] =
            sema::convert(value, s.type).bits;
      }
      return;
    }

    case StmtKind::Call: {
      const Expr& call = *stmt.value;
      if (call.name == "write") {
        const TypedValue value = eval(*call.args[1]);
        const Symbol& s = symbol(call.args[0]->symbol);
        written_[static_cast<std::size_t>(call.args[0]->symbol)].push_back(
            sema::convert(value, s.type).bits);
      } else {
        eval(call);
      }
      return;
    }

    case StmtKind::If:
      if (sema::is_true(eval(*stmt.value))) {
        exec(*stmt.then_branch);
      } else if (stmt.else_branch) {
        exec(*stmt.else_branch);
      }
      return;

    case StmtKind::For:
      exec(*stmt.for_init);
      while (sema::is_true(eval(*stmt.value))) {
        step();
        exec(*stmt.body);
        if (returned_) return;
        exec(*stmt.for_step);
      }
      return;

    case StmtKind::Return:
      if (stmt.value) {
        return_value_ = sema::convert(eval(*stmt.value), function_.return_type).bits;
      }
      returned_ = true;
      return;

    case StmtKind::Pragma:
      return;
  }
}

std::size_t AstInterpreter::element_index(const Expr& index_expr, const Symbol& array) {
  const TypedValue index = eval(index_expr);
  const Int128 position = to_int128(index.bits, index.type.width, index.type.is_signed);
  if (position < 0 || position >= static_cast<Int128>(array.array_size)) {
    throw RuntimeError{"index " + int128_to_string(position) + " is out of bounds for '" +
                       array.name + "'"};
  }
  return static_cast<std::size_t>(position);
}

TypedValue AstInterpreter::eval(const Expr& expr) {
  const IntType type{expr.width, expr.is_signed};
  switch (expr.kind) {
    case ExprKind::IntLiteral:
      return {truncate(expr.value, expr.width), type};

    case ExprKind::Name:
      return scalars_[static_cast<std::size_t>(expr.symbol)];

    case ExprKind::Index: {
      const Symbol& s = symbol(expr.lhs->symbol);
      const std::size_t index = element_index(*expr.rhs, s);
      return {arrays_[static_cast<std::size_t>(expr.lhs->symbol)][index], s.type};
    }

    case ExprKind::Call: {
      // Only read() produces a value; sema has rejected everything else.
      const int stream = expr.args[0]->symbol;
      const Symbol& s = symbol(stream);
      const auto& data = inputs_.streams.at(static_cast<std::size_t>(s.param_index));
      std::size_t& position = read_position_[static_cast<std::size_t>(stream)];
      if (position >= data.size()) {
        throw RuntimeError{"read past the end of the input on stream '" + s.name + "'"};
      }
      return {truncate(data[position++], s.type.width), s.type};
    }

    case ExprKind::Unary:
      return sema::eval_unary(expr.unary_op, eval(*expr.lhs), type);

    case ExprKind::Binary: {
      // Left to right, and both sides always: `&&` and `||` do not
      // short-circuit.
      const TypedValue lhs = eval(*expr.lhs);
      const TypedValue rhs = eval(*expr.rhs);
      return sema::eval_binary(expr.binary_op, lhs, rhs, type);
    }

    case ExprKind::Conditional: {
      const TypedValue condition = eval(*expr.lhs);
      const TypedValue then_value = eval(*expr.rhs);
      const TypedValue else_value = eval(*expr.third);
      return sema::convert(sema::is_true(condition) ? then_value : else_value, type);
    }
  }
  return {0, type};
}

}  // namespace

Outputs run_ast(const sema::FunctionInfo& function, const Inputs& inputs) {
  return AstInterpreter(function, inputs).run();
}

}  // namespace minihls::interp
