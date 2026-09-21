#include "driver/compile.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "sema/const_eval.hpp"
#include "sema/resolver.hpp"
#include "sema/type_check.hpp"
#include <iostream>

namespace minihls {

Compilation compileText(std::string name, std::string text) {
  Compilation c;
  c.source = std::make_unique<SourceFile>(std::move(name), std::move(text));
  c.diags  = std::make_unique<Diagnostics>(*c.source);

  auto tokens = Lexer(*c.source, *c.diags).tokenize();
  c.program = Parser(std::move(tokens), *c.source, *c.diags).parseProgram();

  // Each pass annotates the same tree; none of them calls the others.
  Resolver(*c.diags).resolve(c.program);
  TypeChecker(*c.diags).check(c.program);
  ConstantEvaluator(*c.diags).evaluate(c.program);

  c.ok = !c.diags->hasErrors();
  return c;
}

Compilation compileFile(const std::string& path) {
  auto loaded = SourceFile::load(path);
  if (!loaded) {
    std::cerr << "minihls: cannot open " << path << "\n";
    return {};
  }
  return compileText(loaded->path(), loaded->text());
}

} // namespace minihls