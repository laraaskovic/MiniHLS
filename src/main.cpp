#include "driver/compile.hpp"
#include "version.hpp"
#include <cstring>
#include <iostream>

namespace minihls {
int runTests(const std::string& path);
int runProgram(const std::string& path, const std::vector<std::string>& args);
#ifdef MINIHLS_WITH_MLIR
int dumpMlirFile(const std::string& path);
#endif
}

static int usage() {
  std::cerr <<
    "usage: minihls --version\n"
    "       minihls check <file.hc>          parse and check, report diagnostics\n"
    "       minihls run   <file.hc> [args]   check, then execute\n"
    "       minihls test  <file.hc>          run the matching .tests file\n";
  return 1;
}

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  std::string cmd = argv[1];

  if (cmd == "--version") { std::cout << "minihls " << minihls::version() << "\n"; return 0; }

  if (cmd == "check" && argc == 3) {
    auto c = minihls::compileFile(argv[2]);
    if (!c.source) return 1;
    c.diags->print(std::cerr);
    if (c.ok) std::cout << argv[2] << ": ok\n";
    return c.ok ? 0 : 1;
  }

  if (cmd == "test" && argc == 3) return minihls::runTests(argv[2]);

  if (cmd == "run" && argc >= 3) {
    std::vector<std::string> args(argv + 3, argv + argc);
    return minihls::runProgram(argv[2], args);
  }

  return usage();
}