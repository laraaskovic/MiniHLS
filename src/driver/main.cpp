// The minihls command-line driver.
//
// Milestone 1 exposes only the frontend. Later milestones add the flags the
// README describes (--emit-ir, --emit-sv, --report schedule); the argument
// handling here is deliberately minimal until there is something to hang them
// on.

#include "frontend/ast_printer.hpp"
#include "frontend/diagnostics.hpp"
#include "frontend/parser.hpp"
#include "frontend/source.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kUsage =
    R"(usage: minihls <command> [options] <file.hc>

commands:
  parse <file.hc>     parse the file and report any diagnostics

options:
  --print-ast         print the parsed program back out as source
  -h, --help          show this message

exit status is 0 only if the file parsed without errors.
)";

int fail_usage(std::string_view message) {
  std::cerr << "minihls: " << message << "\n\n" << kUsage;
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

  for (const std::string& arg : args) {
    if (arg == "-h" || arg == "--help") {
      std::cout << kUsage;
      return 0;
    }
  }

  if (args.empty()) return fail_usage("no command given");
  if (args.front() != "parse") {
    return fail_usage("unknown command '" + args.front() + "'");
  }

  bool print_ast = false;
  std::string path;
  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];
    if (arg == "--print-ast") {
      print_ast = true;
    } else if (arg.starts_with("-")) {
      return fail_usage("unknown option '" + arg + "'");
    } else if (path.empty()) {
      path = arg;
    } else {
      return fail_usage("more than one input file given");
    }
  }
  if (path.empty()) return fail_usage("no input file given");

  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    std::cerr << "minihls: cannot open '" << path << "'\n";
    return 1;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();

  minihls::SourceFile file(path, buffer.str());
  minihls::DiagnosticEngine diagnostics(file);
  const auto program = minihls::parse(file, diagnostics);

  // Diagnostics go to stderr so that --print-ast output stays pipeable.
  std::cerr << diagnostics.format_all();
  if (diagnostics.has_errors()) {
    std::cerr << "minihls: " << diagnostics.error_count() << " error(s) in '" << path
              << "'\n";
    return 1;
  }

  if (print_ast) std::cout << minihls::ast::print(*program);
  return 0;
}
