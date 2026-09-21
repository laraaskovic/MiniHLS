#pragma once
#include "frontend/ast.hpp"
#include "frontend/diagnostics.hpp"
#include "frontend/source.hpp"
#include <memory>
#include <string>

namespace minihls {

// Owns everything the AST points into. Member order matters: `program` is
// destroyed first, then diags, then source — the reverse of the dependency
// order, which is what you want.
struct Compilation {
  std::unique_ptr<SourceFile> source;
  std::unique_ptr<Diagnostics> diags;
  Program program;
  bool ok = false;
};

Compilation compileFile(const std::string& path);

} // namespace minihls