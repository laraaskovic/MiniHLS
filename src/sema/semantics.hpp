#pragma once

#include "frontend/ast.hpp"

#include <cstdint>
#include <optional>
#include <string>

// The language's value semantics at the level of source operators.
//
// Semantic analysis uses these to type expressions and to fold constants; the
// AST interpreter uses them to execute. The MLIR execution check deliberately
// does not: it runs the lowered `arith` operations (sign extension,
// truncation, a same-width add) through LLVM, so the two are independent and a
// lowering bug shows up as a disagreement between them.
namespace minihls::sema {

struct IntType {
  unsigned width = 0;
  bool is_signed = false;

  friend bool operator==(IntType, IntType) = default;
};

// "i17", "u1".
std::string to_string(IntType type);

// A value of a known type. Only the low `type.width` bits of `bits` are
// meaningful; the rest are always zero.
struct TypedValue {
  std::uint64_t bits = 0;
  IntType type;
};

// The smallest unsigned type that represents a literal.
IntType literal_type(std::uint64_t value);

// Result type of an operator applied to operands of the given types, following
// the table in LANGUAGE.md. `shift_amount` is the value of a constant right
// operand, which only matters for `<<`. Returns nullopt, with `error` set, when
// the result would be wider than 64 bits.
std::optional<IntType> binary_result_type(ast::BinaryOp op, IntType lhs, IntType rhs,
                                          std::optional<std::uint64_t> shift_amount,
                                          std::string& error);
std::optional<IntType> unary_result_type(ast::UnaryOp op, IntType operand,
                                         std::string& error);
std::optional<IntType> conditional_result_type(IntType then_type, IntType else_type,
                                               std::string& error);

// Evaluation. The result is the exact mathematical result of the operator,
// brought into `result` by keeping its low bits -- which, because arithmetic
// grows, only ever discards anything for the documented truncating cases.
TypedValue eval_unary(ast::UnaryOp op, TypedValue operand, IntType result);
TypedValue eval_binary(ast::BinaryOp op, TypedValue lhs, TypedValue rhs, IntType result);

// Assignment conversion: truncate, or extend according to the source's own
// signedness.
TypedValue convert(TypedValue value, IntType destination);

bool is_true(TypedValue value);

// Decimal rendering at the value's own signedness.
std::string to_string(TypedValue value);

}  // namespace minihls::sema
