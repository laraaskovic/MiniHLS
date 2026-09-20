#pragma once

#include "frontend/ast.hpp"
#include <string>

namespace minihls {

std::string printExpr(const Expr& expr);
std::string printProgram(const Program& program);

} // namespace minihls