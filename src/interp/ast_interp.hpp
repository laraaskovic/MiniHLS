#pragma once

#include "interp/io.hpp"
#include "sema/sema.hpp"

// Reference model 1: a tree-walking interpreter over the checked AST.
//
// It executes the program exactly as the language defines it, using the width
// rules in sema/semantics.hpp and nothing else. It is deliberately naive --
// no lowering, no optimization -- so that it is easy to believe, and every
// other stage is judged against it.
namespace minihls::interp {

Outputs run_ast(const sema::FunctionInfo& function, const Inputs& inputs);

}  // namespace minihls::interp
