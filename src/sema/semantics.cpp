#include "sema/semantics.hpp"

#include "support/int128.hpp"

#include <algorithm>
#include <bit>

namespace minihls::sema {
namespace {

// The mixed-signedness step: when exactly one operand is signed, the unsigned
// one `uN` is treated as `i(N+1)`.
void apply_mixed_signedness(IntType& lhs, IntType& rhs) {
  if (lhs.is_signed == rhs.is_signed) return;
  if (!lhs.is_signed) lhs = {lhs.width + 1, true};
  if (!rhs.is_signed) rhs = {rhs.width + 1, true};
}

std::optional<IntType> checked(IntType type, std::string& error) {
  if (type.width > kMaxWidth) {
    error = "result would be " + std::to_string(type.width) +
            " bits wide, but the widest supported value is 64 bits";
    return std::nullopt;
  }
  return type;
}

Int128 math(TypedValue value) {
  return to_int128(value.bits, value.type.width, value.type.is_signed);
}

TypedValue make(Int128 value, IntType type) { return {from_int128(value, type.width), type}; }

// Shift amounts are always read as unsigned, as in SystemVerilog.
std::uint64_t shift_amount(TypedValue amount) {
  return zero_extend(amount.bits, amount.type.width);
}

}  // namespace

std::string to_string(IntType type) {
  return (type.is_signed ? "i" : "u") + std::to_string(type.width);
}

IntType literal_type(std::uint64_t value) {
  const auto width = static_cast<unsigned>(std::bit_width(value));
  return {width == 0 ? 1u : width, false};
}

std::optional<IntType> binary_result_type(ast::BinaryOp op, IntType lhs, IntType rhs,
                                          std::optional<std::uint64_t> shift_amount_value,
                                          std::string& error) {
  using ast::BinaryOp;
  switch (op) {
    case BinaryOp::ShiftLeft:
      if (shift_amount_value) {
        if (*shift_amount_value > kMaxWidth) {
          error = "shifting left by " + std::to_string(*shift_amount_value) +
                  " would exceed 64 bits";
          return std::nullopt;
        }
        return checked({lhs.width + static_cast<unsigned>(*shift_amount_value), lhs.is_signed},
                       error);
      }
      return lhs;
    case BinaryOp::ShiftRight:
      return lhs;
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
    case BinaryOp::LogicalAnd:
    case BinaryOp::LogicalOr: {
      // The result is one bit, but a comparison still has to hold both operands
      // at a common signed width, which must itself be representable.
      apply_mixed_signedness(lhs, rhs);
      if (auto ok = checked({std::max(lhs.width, rhs.width), lhs.is_signed}, error); !ok) {
        return std::nullopt;
      }
      return IntType{1, false};
    }
    default:
      break;
  }

  apply_mixed_signedness(lhs, rhs);
  const bool is_signed = lhs.is_signed || rhs.is_signed;
  const unsigned wider = std::max(lhs.width, rhs.width);
  switch (op) {
    case BinaryOp::Add:
      return checked({wider + 1, is_signed}, error);
    case BinaryOp::Subtract:
      // u8 - u8 can be negative, so subtraction is always signed.
      return checked({wider + 1, true}, error);
    case BinaryOp::Multiply:
      return checked({lhs.width + rhs.width, is_signed}, error);
    case BinaryOp::Divide:
    case BinaryOp::Modulo:
      // The divisor is widened with the dividend, so it must fit as well.
      if (auto ok = checked({wider, is_signed}, error); !ok) return std::nullopt;
      return IntType{lhs.width, is_signed};
    case BinaryOp::BitAnd:
    case BinaryOp::BitOr:
    case BinaryOp::BitXor:
      return checked({wider, is_signed}, error);
    default:
      break;
  }
  error = "internal error: unhandled operator";
  return std::nullopt;
}

std::optional<IntType> unary_result_type(ast::UnaryOp op, IntType operand,
                                         std::string& error) {
  switch (op) {
    case ast::UnaryOp::Negate:
      return checked({operand.width + 1, true}, error);
    case ast::UnaryOp::BitNot:
      return operand;
    case ast::UnaryOp::LogicalNot:
      return IntType{1, false};
  }
  return std::nullopt;
}

std::optional<IntType> conditional_result_type(IntType then_type, IntType else_type,
                                               std::string& error) {
  apply_mixed_signedness(then_type, else_type);
  return checked({std::max(then_type.width, else_type.width),
                  then_type.is_signed || else_type.is_signed},
                 error);
}

TypedValue eval_unary(ast::UnaryOp op, TypedValue operand, IntType result) {
  const Int128 value = math(operand);
  switch (op) {
    case ast::UnaryOp::Negate:
      return make(-value, result);
    case ast::UnaryOp::BitNot:
      return make(~value, result);
    case ast::UnaryOp::LogicalNot:
      return make(value == 0 ? 1 : 0, result);
  }
  return make(0, result);
}

TypedValue eval_binary(ast::BinaryOp op, TypedValue lhs, TypedValue rhs, IntType result) {
  using ast::BinaryOp;
  const Int128 a = math(lhs);
  const Int128 b = math(rhs);
  switch (op) {
    case BinaryOp::Add:
      return make(a + b, result);
    case BinaryOp::Subtract:
      return make(a - b, result);
    case BinaryOp::Multiply:
      return make(a * b, result);
    case BinaryOp::Divide:
      // Division by zero yields all ones, and the remainder the dividend: the
      // RISC-V convention, chosen because it is what a divider that is simply
      // left to run produces, and because it is fully defined.
      if (b == 0) return make(-1, result);
      return make(a / b, result);
    case BinaryOp::Modulo:
      if (b == 0) return make(a, result);
      return make(a % b, result);
    case BinaryOp::BitAnd:
      return make(a & b, result);
    case BinaryOp::BitOr:
      return make(a | b, result);
    case BinaryOp::BitXor:
      return make(a ^ b, result);
    case BinaryOp::ShiftLeft: {
      const std::uint64_t amount = shift_amount(rhs);
      if (amount >= result.width) return make(0, result);
      return make(a << amount, result);
    }
    case BinaryOp::ShiftRight: {
      const std::uint64_t amount = shift_amount(rhs);
      if (amount >= lhs.type.width) return make(a < 0 ? -1 : 0, result);
      return make(a >> amount, result);
    }
    case BinaryOp::Equal:
      return make(a == b, result);
    case BinaryOp::NotEqual:
      return make(a != b, result);
    case BinaryOp::Less:
      return make(a < b, result);
    case BinaryOp::LessEqual:
      return make(a <= b, result);
    case BinaryOp::Greater:
      return make(a > b, result);
    case BinaryOp::GreaterEqual:
      return make(a >= b, result);
    case BinaryOp::LogicalAnd:
      return make(a != 0 && b != 0, result);
    case BinaryOp::LogicalOr:
      return make(a != 0 || b != 0, result);
  }
  return make(0, result);
}

TypedValue convert(TypedValue value, IntType destination) {
  return make(math(value), destination);
}

bool is_true(TypedValue value) { return value.bits != 0; }

std::string to_string(TypedValue value) {
  return value_to_string(value.bits, value.type.width, value.type.is_signed);
}

}  // namespace minihls::sema
