#pragma once

#include "frontend/ast.hpp"

#include <string>

namespace minihls::ast {

// Renders an AST back to source text.
//
// The output is canonical: parsing it must produce a tree that prints
// identically. That property is what the round-trip test checks, and it catches
// a large class of parser and printer bugs without anyone hand-writing expected
// output. It is also why the printer consults binary_op_precedence() rather than
// parenthesizing everything -- fully parenthesized output would be trivially
// stable but unreadable, and would hide precedence bugs in the parser.
std::string print(const Program& program);
std::string print(const Function& function);
std::string print(const Stmt& stmt);
std::string print(const Expr& expr);

// How tightly an expression binds, on the same scale as
// binary_op_precedence(). Atoms are highest; the conditional operator lowest.
int expr_precedence(const Expr& expr);

// "i16 a[64]", "stream<u8> input", "const i8 coeffs[8]", "i32 x".
std::string declaration_to_string(const Type& type, const std::string& name);

}  // namespace minihls::ast
