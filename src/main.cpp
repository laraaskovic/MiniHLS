#include <cstring>
#include <iostream>
#include "frontend/lexer.hpp"
#include "version.hpp"
#ifdef MINIHLS_WITH_MLIR
#include "dump.hpp"
#endif

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::cout << "minihls " << minihls::version() << "\n";
    return 0;
  }
  if (argc == 3 && std::strcmp(argv[1], "lex") == 0) {
    auto source = minihls::SourceFile::load(argv[2]);
    if (!source) {
      std::cerr << "could not read " << argv[2] << "\n";
      return 1;
    }
    minihls::Diagnostics diagnostics(*source);
    auto tokens = minihls::Lexer(*source, diagnostics).tokenize();
    for (const auto& token : tokens) {
      unsigned line = source->line(token.range.begin);
      unsigned column = source->column(token.range.begin);
      std::cout << line << ':' << column << "  "
                << minihls::tokName(token.kind) << "  [" << token.text << "]\n";
    }
    if (diagnostics.hasErrors()) {
      diagnostics.print(std::cerr);
      return 1;
    }
    return 0;
  }
#ifdef MINIHLS_WITH_MLIR
  if (argc == 3 && std::strcmp(argv[1], "dump") == 0)
    return minihls::dumpMlirFile(argv[2]);
#endif
  std::cerr << "usage: minihls --version\n";
  std::cerr << "       minihls lex <file.hc>\n";
#ifdef MINIHLS_WITH_MLIR
  std::cerr << "       minihls dump <file.mlir>\n";
#endif
  return 1;
}