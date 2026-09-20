#include <cstring>
#include <iostream>
#include "version.hpp"
#ifdef MINIHLS_WITH_MLIR
#include "dump.hpp"
#endif

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::cout << "minihls " << minihls::version() << "\n";
    return 0;
  }
#ifdef MINIHLS_WITH_MLIR
  if (argc == 3 && std::strcmp(argv[1], "dump") == 0)
    return minihls::dumpMlirFile(argv[2]);
#endif
  std::cerr << "usage: minihls --version\n";
#ifdef MINIHLS_WITH_MLIR
  std::cerr << "       minihls dump <file.mlir>\n";
#endif
  return 1;
}