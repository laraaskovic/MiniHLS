#pragma once

#include "frontend/source.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// The abstract syntax tree.
//
// Nodes carry an explicit `kind` tag and are dispatched with a `switch` rather
// than virtual methods. That is deliberate: with -Wswitch, adding a node kind
// and forgetting to handle it somewhere becomes a compile-time warning, which
// matters a lot once lowering, the interpreter, and the printer all walk this
// tree.
namespace minihls::ast {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

enum class TypeKind { Int, Array, Stream, Void };

struct Type;
using TypePtr = std::unique_ptr<Type>;

struct Type {
  TypeKind kind;
  SourceRange range;

  // Int: width and signedness. Also describes the element type of an Array or
  // Stream, since both may only contain integers.
  unsigned width = 0;
  bool is_signed = false;

  // Array: number of elements, and whether it is a ROM.
  std::uint64_t array_size = 0;
  bool is_const = false;
};

TypePtr make_int_type(unsigned width, bool is_signed, SourceRange range);
TypePtr make_array_type(unsigned width, bool is_signed, std::uint64_t size, bool is_const,
                        SourceRange range);
TypePtr make_stream_type(unsigned width, bool is_signed, SourceRange range);
TypePtr make_void_type(SourceRange range);

// Renders a type as it would be written in source: "i32", "i16[64]",
// "stream<u8>". Array types print without the size when `with_array_size` is
// false, which is how they appear in an expression's type.
std::string type_to_string(const Type& type);

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

enum class UnaryOp { Negate, BitNot, LogicalNot };

enum class BinaryOp {
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
  BitAnd,
  BitOr,
  BitXor,
  ShiftLeft,
  ShiftRight,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LogicalAnd,
  LogicalOr,
};

std::string_view unary_op_spelling(UnaryOp op);
std::string_view binary_op_spelling(BinaryOp op);

// Binding power used by both the Pratt parser and the printer. The printer needs
// it to decide where parentheses are required, which is what keeps
// print-then-reparse stable.
int binary_op_precedence(BinaryOp op);

enum class ExprKind { IntLiteral, Name, Index, Call, Unary, Binary, Conditional };

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
  ExprKind kind;
  SourceRange range;

  // IntLiteral
  std::uint64_t value = 0;

  // Name, Call: the identifier.
  std::string name;

  // Unary
  UnaryOp unary_op = UnaryOp::Negate;

  // Binary
  BinaryOp binary_op = BinaryOp::Add;

  // Index: base and index. Unary: operand in `lhs`. Binary: lhs and rhs.
  // Conditional: condition in `lhs`, then-value in `rhs`, else-value in `third`.
  ExprPtr lhs;
  ExprPtr rhs;
  ExprPtr third;

  // Call
  std::vector<ExprPtr> args;
};

ExprPtr make_int_literal(std::uint64_t value, SourceRange range);
ExprPtr make_name(std::string name, SourceRange range);
ExprPtr make_index(ExprPtr base, ExprPtr index, SourceRange range);
ExprPtr make_call(std::string callee, std::vector<ExprPtr> args, SourceRange range);
ExprPtr make_unary(UnaryOp op, ExprPtr operand, SourceRange range);
ExprPtr make_binary(BinaryOp op, ExprPtr lhs, ExprPtr rhs, SourceRange range);
ExprPtr make_conditional(ExprPtr condition, ExprPtr then_value, ExprPtr else_value,
                         SourceRange range);

// ---------------------------------------------------------------------------
// Pragmas
// ---------------------------------------------------------------------------

enum class PragmaKind { Pipeline, Unroll, Partition };

struct Pragma {
  PragmaKind kind = PragmaKind::Pipeline;
  SourceRange range;

  // Pipeline: the requested initiation interval.
  std::uint64_t initiation_interval = 0;

  // Unroll and Partition: the requested factor. Zero means "completely",
  // which is the default when no factor is given.
  std::uint64_t factor = 0;

  // Partition: the array being partitioned.
  std::string array_name;
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

enum class StmtKind { Block, VarDecl, Assign, Call, If, For, Return, Pragma };

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
  StmtKind kind;
  SourceRange range;

  // Block
  std::vector<StmtPtr> statements;

  // VarDecl
  TypePtr type;
  std::string name;
  // A scalar initializer, or the element list of an array initializer. For a
  // scalar declaration at most one entry is present.
  std::vector<ExprPtr> init;
  bool init_is_list = false;

  // Assign: `target = value`, where target is a Name or Index expression.
  // Call: the call expression in `value`.
  // If: condition in `value`. For: condition in `value`.
  // Return: the returned expression in `value`, or null for a bare return.
  ExprPtr target;
  ExprPtr value;

  // If: then and else branches. For: init, step, and body.
  StmtPtr then_branch;
  StmtPtr else_branch;
  StmtPtr for_init;
  StmtPtr for_step;
  StmtPtr body;

  // Pragma
  Pragma pragma;
};

StmtPtr make_block(std::vector<StmtPtr> statements, SourceRange range);
StmtPtr make_var_decl(TypePtr type, std::string name, std::vector<ExprPtr> init,
                      bool init_is_list, SourceRange range);
StmtPtr make_assign(ExprPtr target, ExprPtr value, SourceRange range);
StmtPtr make_call_stmt(ExprPtr call, SourceRange range);
StmtPtr make_if(ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch,
                SourceRange range);
StmtPtr make_for(StmtPtr init, ExprPtr condition, StmtPtr step, StmtPtr body,
                 SourceRange range);
StmtPtr make_return(ExprPtr value, SourceRange range);
StmtPtr make_pragma(Pragma pragma, SourceRange range);

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

struct Param {
  TypePtr type;
  std::string name;
  SourceRange range;
};

struct Function {
  TypePtr return_type;
  std::string name;
  std::vector<Param> params;
  StmtPtr body;  // always a Block
  SourceRange range;
};

struct Program {
  std::vector<std::unique_ptr<Function>> functions;
};

}  // namespace minihls::ast
