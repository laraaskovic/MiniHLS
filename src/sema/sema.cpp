#include "sema/sema.hpp"

#include "support/int128.hpp"

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

namespace minihls::sema {
namespace {

using ast::Expr;
using ast::ExprKind;
using ast::Stmt;
using ast::StmtKind;

// Loops longer than this cannot be unrolled completely: the copies would swamp
// both the compiler and the device.
constexpr std::uint64_t kMaxFullUnroll = 4096;

std::string quoted(std::string_view name) { return "'" + std::string(name) + "'"; }

class Checker {
 public:
  Checker(ast::Function& function, DiagnosticEngine& diagnostics, FunctionInfo& info)
      : function_(&function), diagnostics_(&diagnostics), info_(&info) {}

  void run();

 private:
  struct LoopContext {
    LoopInfo* info = nullptr;
    bool saw_pipeline = false;
    bool saw_unroll = false;
    SourceRange unroll_range;
  };

  // Scopes -----------------------------------------------------------------

  int lookup(const std::string& name) const;
  int declare(Symbol symbol);

  // Statements -------------------------------------------------------------

  void check_block(Stmt& block);
  void check_statement(Stmt& stmt);
  void check_var_decl(Stmt& stmt);
  void check_assign(Stmt& stmt);
  void check_call_statement(Stmt& stmt);
  void check_if(Stmt& stmt);
  void check_for(Stmt& stmt);
  void check_return(Stmt& stmt);
  void check_pragma(Stmt& stmt);

  // Expressions ------------------------------------------------------------

  // Types `expr` and returns false on error. An expression that failed has
  // width 0, and anything built on it is skipped silently so one mistake is
  // reported once.
  bool check_expr(Expr& expr);
  bool check_call(Expr& call, bool as_statement);
  // Resolves the stream argument of read()/write() and records its direction.
  int check_stream_argument(Expr& argument, StreamDirection direction);

  void note_conversion(IntType from, IntType to, SourceRange range);
  bool definitely_returns(const Stmt& stmt) const;

  // Evaluates a loop header expression at compile time, with the induction
  // variable bound to `value`. Returns nullopt if the expression depends on
  // anything else.
  std::optional<TypedValue> eval_header(const Expr& expr, int induction,
                                        TypedValue value) const;

  static IntType type_of(const Expr& expr) { return {expr.width, expr.is_signed}; }

  ast::Function* function_;
  DiagnosticEngine* diagnostics_;
  FunctionInfo* info_;

  std::vector<std::unordered_map<std::string, int>> scopes_;
  std::vector<LoopContext> loops_;
  // Induction variables of the enclosing loops, which the body may not assign.
  std::unordered_set<int> induction_variables_;
  // Non-empty while checking an operand that is not always evaluated for its
  // effect, such as a `&&` operand, where a stream read is rejected.
  std::vector<std::string> no_effects_reason_;
};

// ---------------------------------------------------------------------------
// Scopes
// ---------------------------------------------------------------------------

int Checker::lookup(const std::string& name) const {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    if (const auto found = scope->find(name); found != scope->end()) return found->second;
  }
  return -1;
}

int Checker::declare(Symbol symbol) {
  if (const int existing = lookup(symbol.name); existing >= 0) {
    const Symbol& previous = info_->symbols[static_cast<std::size_t>(existing)];
    const bool same_scope = scopes_.back().contains(symbol.name);
    diagnostics_->error(symbol.range,
                        same_scope ? quoted(symbol.name) + " is already declared in this scope"
                                   : "declaration of " + quoted(symbol.name) +
                                         " shadows an outer declaration; shadowing is an "
                                         "error in this language");
    diagnostics_->note(previous.range, "previous declaration is here");
    return -1;
  }
  const int index = static_cast<int>(info_->symbols.size());
  scopes_.back().emplace(symbol.name, index);
  info_->symbols.push_back(std::move(symbol));
  return index;
}

// ---------------------------------------------------------------------------
// Function
// ---------------------------------------------------------------------------

void Checker::run() {
  info_->function = function_;
  const ast::Type& return_type = *function_->return_type;
  info_->returns_void = return_type.kind == ast::TypeKind::Void;
  if (!info_->returns_void) info_->return_type = {return_type.width, return_type.is_signed};

  scopes_.emplace_back();
  for (std::size_t i = 0; i < function_->params.size(); ++i) {
    const ast::Param& param = function_->params[i];
    Symbol symbol;
    symbol.name = param.name;
    symbol.range = param.range;
    symbol.type = {param.type->width, param.type->is_signed};
    symbol.is_param = true;
    symbol.param_index = static_cast<int>(i);
    switch (param.type->kind) {
      case ast::TypeKind::Int:
        symbol.kind = SymbolKind::Scalar;
        break;
      case ast::TypeKind::Array:
        symbol.kind = SymbolKind::Array;
        symbol.array_size = param.type->array_size;
        symbol.is_const = param.type->is_const;
        break;
      case ast::TypeKind::Stream:
        symbol.kind = SymbolKind::Stream;
        break;
      case ast::TypeKind::Void:
        diagnostics_->error(param.range, "a parameter cannot have type 'void'");
        continue;
    }
    info_->params.push_back(declare(std::move(symbol)));
  }

  check_block(*function_->body);

  if (!info_->returns_void && !definitely_returns(*function_->body)) {
    diagnostics_->error(function_->range,
                        "control can reach the end of non-void function " +
                            quoted(function_->name) + " without returning a value");
  }

  for (const int index : info_->params) {
    if (index < 0) continue;
    const Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
    if (symbol.kind == SymbolKind::Stream && symbol.direction == StreamDirection::Unused) {
      diagnostics_->warning(symbol.range, "stream " + quoted(symbol.name) +
                                              " is never read or written; it becomes an "
                                              "input port that is never ready");
    }
  }
  scopes_.pop_back();
}

bool Checker::definitely_returns(const Stmt& stmt) const {
  switch (stmt.kind) {
    case StmtKind::Return:
      return true;
    case StmtKind::Block:
      for (const auto& child : stmt.statements) {
        if (definitely_returns(*child)) return true;
      }
      return false;
    case StmtKind::If:
      return stmt.else_branch != nullptr && definitely_returns(*stmt.then_branch) &&
             definitely_returns(*stmt.else_branch);
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

void Checker::check_block(Stmt& block) {
  scopes_.emplace_back();
  bool returned = false;
  bool warned = false;
  for (auto& child : block.statements) {
    if (returned && !warned && child->kind != StmtKind::Pragma) {
      diagnostics_->warning(child->range, "statement is unreachable");
      warned = true;
    }
    check_statement(*child);
    if (definitely_returns(*child)) returned = true;
  }
  scopes_.pop_back();
}

void Checker::check_statement(Stmt& stmt) {
  switch (stmt.kind) {
    case StmtKind::Block:
      check_block(stmt);
      return;
    case StmtKind::VarDecl:
      check_var_decl(stmt);
      return;
    case StmtKind::Assign:
      check_assign(stmt);
      return;
    case StmtKind::Call:
      check_call_statement(stmt);
      return;
    case StmtKind::If:
      check_if(stmt);
      return;
    case StmtKind::For:
      check_for(stmt);
      return;
    case StmtKind::Return:
      check_return(stmt);
      return;
    case StmtKind::Pragma:
      check_pragma(stmt);
      return;
  }
}

void Checker::note_conversion(IntType from, IntType to, SourceRange range) {
  if (from.width <= to.width) return;
  info_->truncations.push_back(
      {Severity::Note, range,
       "truncating " + to_string(from) + " to " + to_string(to) + " keeps the low " +
           std::to_string(to.width) + " bits"});
}

void Checker::check_var_decl(Stmt& stmt) {
  const ast::Type& type = *stmt.type;
  Symbol symbol;
  symbol.name = stmt.name;
  symbol.range = stmt.range;
  symbol.type = {type.width, type.is_signed};

  if (type.kind == ast::TypeKind::Array) {
    // Scope 0 holds the parameters and scope 1 the body's top level. An array
    // becomes storage that is initialized once per call, which only matches
    // what the source says if the declaration also runs once per call.
    if (scopes_.size() > 2) {
      diagnostics_->error(stmt.range,
                          "arrays must be declared at the top level of the function body; "
                          "an array is storage that exists once per call");
    }
    symbol.kind = SymbolKind::Array;
    symbol.array_size = type.array_size;
    symbol.is_const = type.is_const;
    symbol.initial.assign(type.array_size, 0);
    if (stmt.init.size() > type.array_size) {
      diagnostics_->error(stmt.init[type.array_size]->range,
                          "too many initializers for " + quoted(stmt.name) + ", which has " +
                              std::to_string(type.array_size) + " elements");
    }
    for (std::size_t i = 0; i < stmt.init.size(); ++i) {
      Expr& element = *stmt.init[i];
      if (!check_expr(element)) continue;
      if (!element.is_constant) {
        diagnostics_->error(element.range,
                            "an array initializer must be a compile-time constant; it "
                            "becomes the memory's initial contents");
        continue;
      }
      const TypedValue value{element.constant, type_of(element)};
      const TypedValue converted = convert(value, symbol.type);
      if (to_int128(converted.bits, symbol.type.width, symbol.type.is_signed) !=
          to_int128(value.bits, value.type.width, value.type.is_signed)) {
        diagnostics_->warning(element.range, "initializer " + to_string(value) +
                                                 " does not fit in " + to_string(symbol.type) +
                                                 " and becomes " + to_string(converted));
      }
      if (i < symbol.initial.size()) symbol.initial[i] = converted.bits;
    }
  } else {
    symbol.kind = SymbolKind::Scalar;
    if (!stmt.init.empty()) {
      Expr& init = *stmt.init.front();
      if (check_expr(init)) note_conversion(type_of(init), symbol.type, init.range);
    }
  }
  stmt.symbol = declare(std::move(symbol));
}

void Checker::check_assign(Stmt& stmt) {
  Expr& target = *stmt.target;
  const bool value_ok = check_expr(*stmt.value);

  if (target.kind == ExprKind::Name) {
    const int index = lookup(target.name);
    if (index < 0) {
      diagnostics_->error(target.range, "use of undeclared name " + quoted(target.name));
      return;
    }
    target.symbol = index;
    const Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
    if (symbol.kind == SymbolKind::Array) {
      diagnostics_->error(target.range, "cannot assign to array " + quoted(symbol.name) +
                                            " as a whole; assign its elements");
      return;
    }
    if (symbol.kind == SymbolKind::Stream) {
      diagnostics_->error(target.range, "cannot assign to stream " + quoted(symbol.name) +
                                            "; use write(" + symbol.name + ", value)");
      return;
    }
    if (induction_variables_.contains(index)) {
      diagnostics_->error(target.range,
                          "the loop body may not assign the induction variable " +
                              quoted(symbol.name) +
                              "; that would make the trip count a runtime property");
      return;
    }
    target.width = symbol.type.width;
    target.is_signed = symbol.type.is_signed;
  } else if (target.kind == ExprKind::Index) {
    Expr& base = *target.lhs;
    if (base.kind != ExprKind::Name) {
      diagnostics_->error(base.range, "only a named array can be indexed");
      return;
    }
    const int index = lookup(base.name);
    if (index < 0) {
      diagnostics_->error(base.range, "use of undeclared name " + quoted(base.name));
      return;
    }
    base.symbol = index;
    const Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
    if (symbol.kind != SymbolKind::Array) {
      diagnostics_->error(base.range, quoted(symbol.name) + " is not an array");
      return;
    }
    if (symbol.is_const) {
      diagnostics_->error(target.range, "cannot assign to an element of const array " +
                                            quoted(symbol.name) + ", which is a ROM");
      return;
    }
    if (!check_expr(*target.rhs)) return;
    if (target.rhs->is_constant) {
      const Int128 position =
          to_int128(target.rhs->constant, target.rhs->width, target.rhs->is_signed);
      if (position < 0 || position >= static_cast<Int128>(symbol.array_size)) {
        diagnostics_->error(target.rhs->range,
                            "index " + int128_to_string(position) + " is out of bounds for " +
                                quoted(symbol.name) + ", which has " +
                                std::to_string(symbol.array_size) + " elements");
        return;
      }
    }
    target.width = symbol.type.width;
    target.is_signed = symbol.type.is_signed;
  } else {
    diagnostics_->error(target.range, "invalid assignment target");
    return;
  }
  if (value_ok) note_conversion(type_of(*stmt.value), type_of(target), stmt.range);
}

void Checker::check_call_statement(Stmt& stmt) { check_call(*stmt.value, true); }

void Checker::check_if(Stmt& stmt) {
  check_expr(*stmt.value);
  // Each branch gets its own scope, even when it is a single statement.
  scopes_.emplace_back();
  check_statement(*stmt.then_branch);
  scopes_.pop_back();
  if (stmt.else_branch) {
    scopes_.emplace_back();
    check_statement(*stmt.else_branch);
    scopes_.pop_back();
  }
}

void Checker::check_for(Stmt& stmt) {
  scopes_.emplace_back();
  LoopInfo& info = info_->loops[&stmt];
  info.range = stmt.range;

  // The initializer: a declaration or an assignment of the induction variable.
  Stmt& init = *stmt.for_init;
  const Expr* init_value = nullptr;
  if (init.kind == StmtKind::VarDecl) {
    check_var_decl(init);
    if (init.type->kind != ast::TypeKind::Int || init.init.empty()) {
      diagnostics_->error(init.range,
                          "a loop must declare its induction variable with an initial value");
    } else {
      info.induction = init.symbol;
      init_value = init.init.front().get();
    }
  } else {
    check_assign(init);
    if (init.target->kind == ExprKind::Name && init.target->symbol >= 0 &&
        init.target->width != 0) {
      info.induction = init.target->symbol;
      init_value = init.value.get();
    } else if (init.target->kind != ExprKind::Name) {
      diagnostics_->error(init.target->range,
                          "a loop initializer must assign a scalar induction variable");
    }
  }

  const bool condition_ok = check_expr(*stmt.value);
  check_assign(*stmt.for_step);
  const Expr& step_target = *stmt.for_step->target;
  bool header_ok = condition_ok && info.induction >= 0 && init_value != nullptr &&
                   init_value->width != 0 && stmt.for_step->value->width != 0;
  if (info.induction >= 0 &&
      (step_target.kind != ExprKind::Name || step_target.symbol != info.induction)) {
    diagnostics_->error(stmt.for_step->range,
                        "the loop step must assign the induction variable " +
                            quoted(info_->symbols[static_cast<std::size_t>(info.induction)].name));
    header_ok = false;
  }
  if (header_ok && !init_value->is_constant) {
    diagnostics_->error(init_value->range,
                        "the loop's initial value must be a compile-time constant");
    header_ok = false;
  }

  // Run the header at compile time to find the exact trip count.
  std::vector<std::uint64_t> values;
  if (header_ok) {
    const IntType induction_type =
        info_->symbols[static_cast<std::size_t>(info.induction)].type;
    TypedValue current =
        convert({init_value->constant, type_of(*init_value)}, induction_type);
    const std::string& name = info_->symbols[static_cast<std::size_t>(info.induction)].name;
    while (true) {
      const auto condition = eval_header(*stmt.value, info.induction, current);
      if (!condition) {
        diagnostics_->error(stmt.value->range,
                            "the loop condition must depend only on " + quoted(name) +
                                " and constants, so the trip count is known at compile time");
        header_ok = false;
        break;
      }
      if (!is_true(*condition)) break;
      if (values.size() >= kMaxTripCount) {
        diagnostics_->error(stmt.value->range,
                            "the loop does not finish within " +
                                std::to_string(kMaxTripCount) + " iterations");
        header_ok = false;
        break;
      }
      values.push_back(current.bits);
      const auto next = eval_header(*stmt.for_step->value, info.induction, current);
      if (!next) {
        diagnostics_->error(stmt.for_step->value->range,
                            "the loop step must depend only on " + quoted(name) +
                                " and constants, so the trip count is known at compile time");
        header_ok = false;
        break;
      }
      current = convert(*next, induction_type);
    }
    info.final_value = current.bits;
    info.trip_count = values.size();
  }

  // The body, with the induction variable protected and pragmas bound here.
  const bool protect = info.induction >= 0 && !induction_variables_.contains(info.induction);
  if (protect) induction_variables_.insert(info.induction);
  loops_.push_back(LoopContext{&info, false, false, {}});
  scopes_.emplace_back();
  check_statement(*stmt.body);
  scopes_.pop_back();
  const LoopContext context = loops_.back();
  loops_.pop_back();
  if (protect) induction_variables_.erase(info.induction);

  if (header_ok && context.saw_unroll) {
    if (info.unroll_factor == 0 || info.unroll_factor >= info.trip_count) {
      info.unroll_factor = info.trip_count;
    } else if (info.trip_count % info.unroll_factor != 0) {
      diagnostics_->error(context.unroll_range,
                          "unroll factor " + std::to_string(info.unroll_factor) +
                              " does not divide the trip count " +
                              std::to_string(info.trip_count));
    }
    if (info.unroll_factor == info.trip_count) {
      if (info.trip_count > kMaxFullUnroll) {
        diagnostics_->error(context.unroll_range,
                            "cannot unroll " + std::to_string(info.trip_count) +
                                " iterations completely; the limit is " +
                                std::to_string(kMaxFullUnroll));
      }
      info.induction_values = std::move(values);
    }
    // A loop that runs zero times has nothing to unroll.
    if (info.trip_count == 0) info.unroll_factor = 0;
  }
  if (context.saw_unroll && context.saw_pipeline) {
    diagnostics_->error(context.unroll_range,
                        "a loop cannot be both unrolled and pipelined; unroll an inner "
                        "loop and pipeline this one instead");
  }
  scopes_.pop_back();
}

void Checker::check_return(Stmt& stmt) {
  if (!loops_.empty()) {
    diagnostics_->error(stmt.range,
                        "return inside a loop would make the loop's trip count a runtime "
                        "property; compute the result and return after the loop");
  }
  if (stmt.value == nullptr) {
    if (!info_->returns_void) {
      diagnostics_->error(stmt.range, "non-void function " + quoted(function_->name) +
                                          " must return a value");
    }
    return;
  }
  if (info_->returns_void) {
    diagnostics_->error(stmt.value->range,
                        "void function " + quoted(function_->name) + " cannot return a value");
    return;
  }
  if (check_expr(*stmt.value)) {
    note_conversion(type_of(*stmt.value), info_->return_type, stmt.range);
  }
}

void Checker::check_pragma(Stmt& stmt) {
  const ast::Pragma& pragma = stmt.pragma;
  switch (pragma.kind) {
    case ast::PragmaKind::Pipeline:
    case ast::PragmaKind::Unroll: {
      const bool pipeline = pragma.kind == ast::PragmaKind::Pipeline;
      const std::string name = pipeline ? "'#pragma pipeline'" : "'#pragma unroll'";
      if (loops_.empty()) {
        diagnostics_->error(stmt.range, name + " must appear inside the body of the loop "
                                                   "it applies to");
        return;
      }
      LoopContext& loop = loops_.back();
      bool& seen = pipeline ? loop.saw_pipeline : loop.saw_unroll;
      if (seen) {
        diagnostics_->error(stmt.range, "duplicate " + name + " for this loop");
        return;
      }
      seen = true;
      if (pipeline) {
        loop.info->pipeline_ii = pragma.initiation_interval;
      } else {
        loop.info->unroll_factor = pragma.factor;
        loop.unroll_range = stmt.range;
      }
      return;
    }
    case ast::PragmaKind::Partition: {
      const int index = lookup(pragma.array_name);
      if (index < 0) {
        diagnostics_->error(stmt.range, "no array named " + quoted(pragma.array_name) +
                                            " is declared before this pragma");
        return;
      }
      Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
      if (symbol.kind != SymbolKind::Array) {
        diagnostics_->error(stmt.range, quoted(symbol.name) + " is not an array");
        return;
      }
      if (symbol.is_param) {
        diagnostics_->error(stmt.range,
                            "cannot partition parameter " + quoted(symbol.name) +
                                ": an array parameter is a memory interface owned by the "
                                "caller");
        return;
      }
      if (pragma.factor > 1 && pragma.factor < symbol.array_size) {
        diagnostics_->warning(stmt.range,
                              "partial partitioning is not implemented; " +
                                  quoted(symbol.name) + " is partitioned completely");
      }
      symbol.partitioned = true;
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

bool Checker::check_expr(Expr& expr) {
  auto fail = [&expr] {
    expr.width = 0;
    return false;
  };
  auto set_type = [&expr](IntType type) {
    expr.width = type.width;
    expr.is_signed = type.is_signed;
  };

  switch (expr.kind) {
    case ExprKind::IntLiteral:
      set_type(literal_type(expr.value));
      expr.is_constant = true;
      expr.constant = expr.value;
      return true;

    case ExprKind::Name: {
      const int index = lookup(expr.name);
      if (index < 0) {
        diagnostics_->error(expr.range, "use of undeclared name " + quoted(expr.name));
        return fail();
      }
      expr.symbol = index;
      const Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
      if (symbol.kind == SymbolKind::Array) {
        diagnostics_->error(expr.range, "array " + quoted(symbol.name) +
                                            " is not a value; index it, as in " +
                                            symbol.name + "[i]");
        return fail();
      }
      if (symbol.kind == SymbolKind::Stream) {
        diagnostics_->error(expr.range, "stream " + quoted(symbol.name) +
                                            " can only be used with read() or write()");
        return fail();
      }
      set_type(symbol.type);
      return true;
    }

    case ExprKind::Index: {
      Expr& base = *expr.lhs;
      const bool index_ok = check_expr(*expr.rhs);
      if (base.kind != ExprKind::Name) {
        diagnostics_->error(base.range, "only a named array can be indexed");
        return fail();
      }
      const int index = lookup(base.name);
      if (index < 0) {
        diagnostics_->error(base.range, "use of undeclared name " + quoted(base.name));
        return fail();
      }
      base.symbol = index;
      const Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
      if (symbol.kind != SymbolKind::Array) {
        diagnostics_->error(base.range, quoted(symbol.name) + " is not an array");
        return fail();
      }
      if (!index_ok) return fail();
      if (expr.rhs->is_constant) {
        const Int128 position = to_int128(expr.rhs->constant, expr.rhs->width,
                                          expr.rhs->is_signed);
        if (position < 0 || position >= static_cast<Int128>(symbol.array_size)) {
          diagnostics_->error(expr.rhs->range,
                              "index " + int128_to_string(position) +
                                  " is out of bounds for " + quoted(symbol.name) +
                                  ", which has " + std::to_string(symbol.array_size) +
                                  " elements");
          return fail();
        }
      }
      set_type(symbol.type);
      return true;
    }

    case ExprKind::Call:
      return check_call(expr, false);

    case ExprKind::Unary: {
      if (!check_expr(*expr.lhs)) return fail();
      std::string error;
      const auto type = unary_result_type(expr.unary_op, type_of(*expr.lhs), error);
      if (!type) {
        diagnostics_->error(expr.range, error);
        return fail();
      }
      set_type(*type);
      if (expr.lhs->is_constant) {
        expr.is_constant = true;
        expr.constant =
            eval_unary(expr.unary_op, {expr.lhs->constant, type_of(*expr.lhs)}, *type).bits;
      }
      return true;
    }

    case ExprKind::Binary: {
      const bool logical = expr.binary_op == ast::BinaryOp::LogicalAnd ||
                           expr.binary_op == ast::BinaryOp::LogicalOr;
      if (logical) {
        no_effects_reason_.push_back(
            "read() cannot be an operand of '" +
            std::string(ast::binary_op_spelling(expr.binary_op)) +
            "': both operands are always evaluated, so the read would look conditional but "
            "is not");
      }
      const bool lhs_ok = check_expr(*expr.lhs);
      const bool rhs_ok = check_expr(*expr.rhs);
      if (logical) no_effects_reason_.pop_back();
      if (!lhs_ok || !rhs_ok) return fail();

      std::optional<std::uint64_t> shift;
      if (expr.rhs->is_constant) shift = zero_extend(expr.rhs->constant, expr.rhs->width);
      std::string error;
      const auto type = binary_result_type(expr.binary_op, type_of(*expr.lhs),
                                           type_of(*expr.rhs), shift, error);
      if (!type) {
        diagnostics_->error(expr.range, error);
        return fail();
      }
      set_type(*type);

      if ((expr.binary_op == ast::BinaryOp::Divide ||
           expr.binary_op == ast::BinaryOp::Modulo) &&
          !expr.rhs->is_constant) {
        diagnostics_->warning(expr.range,
                              "division by a non-constant becomes a full combinational "
                              "divider, which is large and slow");
      }
      if (expr.lhs->is_constant && expr.rhs->is_constant) {
        expr.is_constant = true;
        expr.constant = eval_binary(expr.binary_op, {expr.lhs->constant, type_of(*expr.lhs)},
                                    {expr.rhs->constant, type_of(*expr.rhs)}, *type)
                            .bits;
      }
      return true;
    }

    case ExprKind::Conditional: {
      const bool condition_ok = check_expr(*expr.lhs);
      no_effects_reason_.push_back(
          "read() cannot appear in a branch of '?:': both branches are always evaluated, "
          "so the read would look conditional but is not");
      const bool then_ok = check_expr(*expr.rhs);
      const bool else_ok = check_expr(*expr.third);
      no_effects_reason_.pop_back();
      if (!condition_ok || !then_ok || !else_ok) return fail();
      std::string error;
      const auto type =
          conditional_result_type(type_of(*expr.rhs), type_of(*expr.third), error);
      if (!type) {
        diagnostics_->error(expr.range, error);
        return fail();
      }
      set_type(*type);
      if (expr.lhs->is_constant && expr.rhs->is_constant && expr.third->is_constant) {
        const Expr& chosen =
            is_true({expr.lhs->constant, type_of(*expr.lhs)}) ? *expr.rhs : *expr.third;
        expr.is_constant = true;
        expr.constant = convert({chosen.constant, type_of(chosen)}, *type).bits;
      }
      return true;
    }
  }
  return fail();
}

int Checker::check_stream_argument(Expr& argument, StreamDirection direction) {
  if (argument.kind != ExprKind::Name) {
    diagnostics_->error(argument.range, "expected the name of a stream parameter");
    return -1;
  }
  const int index = lookup(argument.name);
  if (index < 0) {
    diagnostics_->error(argument.range, "use of undeclared name " + quoted(argument.name));
    return -1;
  }
  argument.symbol = index;
  Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
  if (symbol.kind != SymbolKind::Stream) {
    diagnostics_->error(argument.range, quoted(symbol.name) + " is not a stream");
    return -1;
  }
  if (symbol.direction != StreamDirection::Unused && symbol.direction != direction) {
    diagnostics_->error(argument.range,
                        "stream " + quoted(symbol.name) +
                            " is both read and written; a stream port has one direction");
    return -1;
  }
  symbol.direction = direction;
  argument.width = symbol.type.width;
  argument.is_signed = symbol.type.is_signed;
  return index;
}

bool Checker::check_call(Expr& call, bool as_statement) {
  auto fail = [&call] {
    call.width = 0;
    return false;
  };

  if (call.name == "read") {
    if (!no_effects_reason_.empty()) {
      diagnostics_->error(call.range, no_effects_reason_.back());
      return fail();
    }
    if (call.args.size() != 1) {
      diagnostics_->error(call.range, "read() takes exactly one argument, the stream");
      return fail();
    }
    const int index = check_stream_argument(*call.args[0], StreamDirection::Input);
    if (index < 0) return fail();
    const Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
    call.width = symbol.type.width;
    call.is_signed = symbol.type.is_signed;
    return true;
  }

  if (call.name == "write") {
    if (!as_statement) {
      diagnostics_->error(call.range, "write() does not produce a value");
      return fail();
    }
    if (call.args.size() != 2) {
      diagnostics_->error(call.range,
                          "write() takes exactly two arguments, the stream and the value");
      return fail();
    }
    const bool value_ok = check_expr(*call.args[1]);
    const int index = check_stream_argument(*call.args[0], StreamDirection::Output);
    if (index < 0 || !value_ok) return fail();
    const Symbol& symbol = info_->symbols[static_cast<std::size_t>(index)];
    note_conversion(type_of(*call.args[1]), symbol.type, call.range);
    call.width = 0;
    return true;
  }

  diagnostics_->error(call.range,
                      "unknown function " + quoted(call.name) +
                          "; the only calls are the stream builtins read() and write(), "
                          "since every function compiles to its own module");
  return fail();
}

std::optional<TypedValue> Checker::eval_header(const Expr& expr, int induction,
                                               TypedValue value) const {
  if (expr.width == 0) return std::nullopt;
  if (expr.is_constant) return TypedValue{expr.constant, type_of(expr)};
  switch (expr.kind) {
    case ExprKind::Name:
      if (expr.symbol == induction) return value;
      return std::nullopt;
    case ExprKind::Unary: {
      const auto operand = eval_header(*expr.lhs, induction, value);
      if (!operand) return std::nullopt;
      return eval_unary(expr.unary_op, *operand, type_of(expr));
    }
    case ExprKind::Binary: {
      const auto lhs = eval_header(*expr.lhs, induction, value);
      const auto rhs = eval_header(*expr.rhs, induction, value);
      if (!lhs || !rhs) return std::nullopt;
      return eval_binary(expr.binary_op, *lhs, *rhs, type_of(expr));
    }
    case ExprKind::Conditional: {
      const auto condition = eval_header(*expr.lhs, induction, value);
      const auto then_value = eval_header(*expr.rhs, induction, value);
      const auto else_value = eval_header(*expr.third, induction, value);
      if (!condition || !then_value || !else_value) return std::nullopt;
      return convert(is_true(*condition) ? *then_value : *else_value, type_of(expr));
    }
    default:
      return std::nullopt;
  }
}

}  // namespace

const FunctionInfo* CheckedProgram::find(std::string_view name) const {
  for (const FunctionInfo& info : functions) {
    if (info.function->name == name) return &info;
  }
  return nullptr;
}

bool check(ast::Program& program, DiagnosticEngine& diagnostics, CheckedProgram& out) {
  const std::size_t errors_before = diagnostics.error_count();
  out.functions.clear();
  out.functions.reserve(program.functions.size());

  std::unordered_map<std::string, const ast::Function*> seen;
  for (auto& function : program.functions) {
    if (const auto [it, inserted] = seen.emplace(function->name, function.get()); !inserted) {
      diagnostics.error(function->range,
                        "function " + quoted(function->name) + " is defined more than once");
      diagnostics.note(it->second->range, "previous definition is here");
    }
    out.functions.emplace_back();
    Checker(*function, diagnostics, out.functions.back()).run();
  }
  return diagnostics.error_count() == errors_before;
}

}  // namespace minihls::sema
