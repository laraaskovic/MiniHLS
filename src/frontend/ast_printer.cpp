#include "frontend/ast_printer.hpp"

#include <string>

namespace minihls::ast {
namespace {

// Precedence levels above the binary operator range, so that postfix and atomic
// expressions never get parenthesized.
constexpr int kPostfixPrecedence = 13;
constexpr int kUnaryPrecedence = 12;
constexpr int kConditionalPrecedence = 1;
constexpr int kAtomPrecedence = 100;

constexpr int kIndentWidth = 2;

class Printer {
 public:
  std::string take() { return std::move(out_); }

  void print_program(const Program& program) {
    bool first = true;
    for (const auto& function : program.functions) {
      if (!first) out_ += '\n';
      first = false;
      print_function(*function);
    }
  }

  void print_function(const Function& function) {
    out_ += function.return_type ? type_to_string(*function.return_type) : "void";
    out_ += ' ';
    out_ += function.name;
    out_ += '(';
    for (std::size_t i = 0; i < function.params.size(); ++i) {
      if (i > 0) out_ += ", ";
      out_ += declaration_to_string(*function.params[i].type, function.params[i].name);
    }
    out_ += ") ";
    if (function.body) {
      print_stmt(*function.body, 0);
    } else {
      out_ += "{\n}\n";
    }
  }

  // Prints `stmt` at `depth`, including its own indentation and trailing
  // newline.
  void print_stmt(const Stmt& stmt, int depth) {
    switch (stmt.kind) {
      case StmtKind::Block:
        // A block's opening brace continues whatever line precedes it, so only
        // the contents and closing brace are indented.
        out_ += "{\n";
        for (const auto& inner : stmt.statements) {
          print_stmt(*inner, depth + 1);
        }
        indent(depth);
        out_ += "}\n";
        return;

      case StmtKind::VarDecl:
        indent(depth);
        print_var_decl_inline(stmt);
        out_ += ";\n";
        return;

      case StmtKind::Assign:
        indent(depth);
        print_assign_inline(stmt);
        out_ += ";\n";
        return;

      case StmtKind::Call:
        indent(depth);
        print_expr(*stmt.value, 0);
        out_ += ";\n";
        return;

      case StmtKind::If:
        indent(depth);
        out_ += "if (";
        print_expr(*stmt.value, 0);
        out_ += ") ";
        print_branch(stmt.then_branch.get(), depth);
        if (stmt.else_branch) {
          indent(depth);
          out_ += "else ";
          print_branch(stmt.else_branch.get(), depth);
        }
        return;

      case StmtKind::For:
        indent(depth);
        out_ += "for (";
        print_for_clause(stmt.for_init.get());
        out_ += "; ";
        print_expr(*stmt.value, 0);
        out_ += "; ";
        print_for_clause(stmt.for_step.get());
        out_ += ") ";
        print_branch(stmt.body.get(), depth);
        return;

      case StmtKind::Return:
        indent(depth);
        out_ += "return";
        if (stmt.value) {
          out_ += ' ';
          print_expr(*stmt.value, 0);
        }
        out_ += ";\n";
        return;

      case StmtKind::Pragma:
        indent(depth);
        print_pragma(stmt.pragma);
        out_ += '\n';
        return;
    }
  }

  void print_expr(const Expr& expr, int parent_precedence) {
    const bool needs_parens = expr_precedence(expr) < parent_precedence;
    if (needs_parens) out_ += '(';
    print_expr_inner(expr);
    if (needs_parens) out_ += ')';
  }

 private:
  void indent(int depth) { out_.append(static_cast<std::size_t>(depth * kIndentWidth), ' '); }

  // A branch of an `if` or the body of a `for`. A block continues on the same
  // line as the `)`; anything else moves to its own indented line, which keeps
  // the printed tree structurally identical to the parsed one instead of
  // silently inserting braces.
  void print_branch(const Stmt* branch, int depth) {
    if (branch == nullptr) {
      out_ += "{\n";
      indent(depth);
      out_ += "}\n";
      return;
    }
    if (branch->kind == StmtKind::Block) {
      print_stmt(*branch, depth);
      return;
    }
    out_ += '\n';
    print_stmt(*branch, depth + 1);
  }

  void print_var_decl_inline(const Stmt& stmt) {
    out_ += declaration_to_string(*stmt.type, stmt.name);
    if (stmt.init.empty()) return;
    out_ += " = ";
    if (stmt.init_is_list) {
      out_ += "{ ";
      for (std::size_t i = 0; i < stmt.init.size(); ++i) {
        if (i > 0) out_ += ", ";
        print_expr(*stmt.init[i], 0);
      }
      out_ += " }";
    } else {
      print_expr(*stmt.init.front(), 0);
    }
  }

  void print_assign_inline(const Stmt& stmt) {
    print_expr(*stmt.target, 0);
    out_ += " = ";
    print_expr(*stmt.value, 0);
  }

  // The initializer and step of a `for`, which are written without a trailing
  // semicolon.
  void print_for_clause(const Stmt* stmt) {
    if (stmt == nullptr) return;
    switch (stmt->kind) {
      case StmtKind::VarDecl:
        print_var_decl_inline(*stmt);
        return;
      case StmtKind::Assign:
        print_assign_inline(*stmt);
        return;
      default:
        // The grammar admits nothing else here; printing the statement form
        // would emit a stray semicolon, so emit nothing rather than corrupt
        // the output.
        return;
    }
  }

  void print_pragma(const Pragma& pragma) {
    out_ += "#pragma ";
    switch (pragma.kind) {
      case PragmaKind::Pipeline:
        out_ += "pipeline II=";
        out_ += std::to_string(pragma.initiation_interval);
        return;
      case PragmaKind::Unroll:
        out_ += "unroll";
        if (pragma.factor != 0) {
          out_ += " factor=";
          out_ += std::to_string(pragma.factor);
        }
        return;
      case PragmaKind::Partition:
        out_ += "partition ";
        out_ += pragma.array_name;
        if (pragma.factor != 0) {
          out_ += " factor=";
          out_ += std::to_string(pragma.factor);
        }
        return;
    }
  }

  void print_expr_inner(const Expr& expr) {
    switch (expr.kind) {
      case ExprKind::IntLiteral:
        out_ += std::to_string(expr.value);
        return;

      case ExprKind::Name:
        out_ += expr.name;
        return;

      case ExprKind::Index:
        print_expr(*expr.lhs, kPostfixPrecedence);
        out_ += '[';
        print_expr(*expr.rhs, 0);
        out_ += ']';
        return;

      case ExprKind::Call:
        out_ += expr.name;
        out_ += '(';
        for (std::size_t i = 0; i < expr.args.size(); ++i) {
          if (i > 0) out_ += ", ";
          print_expr(*expr.args[i], 0);
        }
        out_ += ')';
        return;

      case ExprKind::Unary:
        out_ += unary_op_spelling(expr.unary_op);
        // Parenthesize any operand that binds no tighter than a unary operator.
        // `- -a` would otherwise print as `--a`; that happens to re-lex
        // correctly today only because the language has no decrement operator.
        print_expr(*expr.lhs, kUnaryPrecedence + 1);
        return;

      case ExprKind::Binary: {
        const int precedence = binary_op_precedence(expr.binary_op);
        // Every binary operator here is left-associative, so an operand of
        // equal precedence needs parentheses on the right but not on the left.
        print_expr(*expr.lhs, precedence);
        out_ += ' ';
        out_ += binary_op_spelling(expr.binary_op);
        out_ += ' ';
        print_expr(*expr.rhs, precedence + 1);
        return;
      }

      case ExprKind::Conditional:
        // The condition must bind tighter than `?:`. The two arms do not: the
        // grammar reads the middle as a full expression and the right arm
        // right-associatively.
        print_expr(*expr.lhs, kConditionalPrecedence + 1);
        out_ += " ? ";
        print_expr(*expr.rhs, 0);
        out_ += " : ";
        print_expr(*expr.third, kConditionalPrecedence);
        return;
    }
  }

  std::string out_;
};

}  // namespace

int expr_precedence(const Expr& expr) {
  switch (expr.kind) {
    case ExprKind::IntLiteral:
    case ExprKind::Name:
      return kAtomPrecedence;
    case ExprKind::Index:
    case ExprKind::Call:
      return kPostfixPrecedence;
    case ExprKind::Unary:
      return kUnaryPrecedence;
    case ExprKind::Binary:
      return binary_op_precedence(expr.binary_op);
    case ExprKind::Conditional:
      return kConditionalPrecedence;
  }
  return kAtomPrecedence;
}

std::string declaration_to_string(const Type& type, const std::string& name) {
  const std::string element = (type.is_signed ? "i" : "u") + std::to_string(type.width);
  switch (type.kind) {
    case TypeKind::Int:
      return element + " " + name;
    case TypeKind::Array:
      return (type.is_const ? "const " : "") + element + " " + name + "[" +
             std::to_string(type.array_size) + "]";
    case TypeKind::Stream:
      return "stream<" + element + "> " + name;
    case TypeKind::Void:
      return "void " + name;
  }
  return element + " " + name;
}

std::string print(const Program& program) {
  Printer printer;
  printer.print_program(program);
  return printer.take();
}

std::string print(const Function& function) {
  Printer printer;
  printer.print_function(function);
  return printer.take();
}

std::string print(const Stmt& stmt) {
  Printer printer;
  printer.print_stmt(stmt, 0);
  return printer.take();
}

std::string print(const Expr& expr) {
  Printer printer;
  printer.print_expr(expr, 0);
  return printer.take();
}

}  // namespace minihls::ast
