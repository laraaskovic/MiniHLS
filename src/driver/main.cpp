// The minihls command-line driver.
//
// Each command runs the pipeline as far as it needs: `parse` stops after the
// parser, `check` after semantic analysis, and `run` executes the program with
// the AST interpreter -- the golden reference every later stage is compared
// against. `emit-mlir`, built when MLIR is available, prints the program in
// MLIR's software-level dialects. Later milestones add `compile`.

#include "frontend/ast_printer.hpp"
#include "frontend/diagnostics.hpp"
#include "frontend/parser.hpp"
#include "frontend/source.hpp"
#include "interp/ast_interp.hpp"
#include "sema/sema.hpp"
#include "support/int128.hpp"

#ifdef MINIHLS_HAVE_MLIR
#include "mlirgen/mlirgen.hpp"
#include "transforms/transforms.hpp"

#include "mlir/IR/MLIRContext.h"
#include "llvm/Support/raw_ostream.h"
#endif

#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kUsage =
    R"(usage: minihls <command> [options] <file.hc>

commands:
  parse <file.hc>             parse the file and report any diagnostics
  check <file.hc>             parse and type-check the file
  run <file.hc> --top <f>     run function <f> with the reference interpreter
  emit-mlir <file.hc> --top <f>
                              print function <f> as MLIR (func/arith/scf/memref)

options:
  --print-ast                 parse: print the parsed program back out as source
  --report-truncations        check: list every assignment that drops bits
  --top <name>                run, emit-mlir: the function to use
  --optimize                  emit-mlir: run the optimization pipeline first
                              (canonicalize, cse, unroll, bit-width narrowing,
                              sccp)
  --report ops                emit-mlir: operator counts and total datapath
                              bits, before and after --optimize
  --arg <name>=<value>        run: a parameter value; repeat for each parameter.
                              Scalars take one integer (decimal, 0x hex, or 0b
                              binary, optionally negative). Arrays and input
                              streams take a comma-separated list; missing
                              array elements are zero.
  -h, --help                  show this message

exit status is 0 only if every requested step succeeded.
)";

int fail_usage(std::string_view message) {
  std::cerr << "minihls: " << message << "\n\n" << kUsage;
  return 2;
}

struct Options {
  std::string command;
  std::string path;
  bool print_ast = false;
  bool report_truncations = false;
  bool optimize = false;
  std::vector<std::string> reports;
  std::string top;
  std::vector<std::pair<std::string, std::string>> args;
};

// Parses one integer as written on the command line.
std::optional<minihls::Int128> parse_integer(std::string_view text) {
  bool negative = false;
  if (!text.empty() && text.front() == '-') {
    negative = true;
    text.remove_prefix(1);
  }
  unsigned base = 10;
  if (text.starts_with("0x") || text.starts_with("0X")) {
    base = 16;
    text.remove_prefix(2);
  } else if (text.starts_with("0b") || text.starts_with("0B")) {
    base = 2;
    text.remove_prefix(2);
  }
  if (text.empty()) return std::nullopt;
  minihls::Int128 value = 0;
  for (const char c : text) {
    if (c == '_') continue;
    unsigned digit = 0;
    if (c >= '0' && c <= '9') {
      digit = static_cast<unsigned>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<unsigned>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      digit = static_cast<unsigned>(c - 'A' + 10);
    } else {
      return std::nullopt;
    }
    if (digit >= base) return std::nullopt;
    value = value * base + digit;
    // Anything past 65 bits cannot fit any type; stop before overflowing.
    if (value > (static_cast<minihls::Int128>(1) << 66)) return std::nullopt;
  }
  return negative ? -value : value;
}

// Converts a command-line integer to the bits of `type`, rejecting values the
// type cannot represent rather than silently wrapping them.
std::optional<std::uint64_t> to_bits(std::string_view text, minihls::sema::IntType type,
                                     std::string& error) {
  const auto value = parse_integer(text);
  if (!value) {
    error = "'" + std::string(text) + "' is not an integer";
    return std::nullopt;
  }
  const minihls::Int128 low =
      type.is_signed ? -(static_cast<minihls::Int128>(1) << (type.width - 1)) : 0;
  const minihls::Int128 high = type.is_signed
                                   ? (static_cast<minihls::Int128>(1) << (type.width - 1)) - 1
                                   : (static_cast<minihls::Int128>(1) << type.width) - 1;
  if (*value < low || *value > high) {
    error = std::string(text) + " does not fit in " + minihls::sema::to_string(type);
    return std::nullopt;
  }
  return minihls::from_int128(*value, type.width);
}

std::vector<std::string_view> split(std::string_view text, char separator) {
  std::vector<std::string_view> parts;
  if (text.empty()) return parts;
  std::size_t start = 0;
  while (true) {
    const std::size_t end = text.find(separator, start);
    parts.push_back(text.substr(start, end - start));
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return parts;
}

std::string format_list(const std::vector<std::uint64_t>& values,
                        minihls::sema::IntType type) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) out += ", ";
    out += minihls::value_to_string(values[i], type.width, type.is_signed);
  }
  return out + "]";
}

// Builds the interpreter inputs from --arg flags. Every parameter must be
// given, except arrays (which default to zeros) and output streams.
bool build_inputs(const minihls::sema::FunctionInfo& function, const Options& options,
                  minihls::interp::Inputs& inputs) {
  using minihls::sema::SymbolKind;
  const std::size_t count = function.params.size();
  inputs.scalars.assign(count, 0);
  inputs.arrays.assign(count, {});
  inputs.streams.assign(count, {});
  std::vector<bool> given(count, false);

  for (const auto& [name, text] : options.args) {
    bool found = false;
    for (const int index : function.params) {
      const auto& symbol = function.symbols[static_cast<std::size_t>(index)];
      if (symbol.name != name) continue;
      found = true;
      const auto position = static_cast<std::size_t>(symbol.param_index);
      given[position] = true;
      std::string error;
      if (symbol.kind == SymbolKind::Scalar) {
        const auto bits = to_bits(text, symbol.type, error);
        if (!bits) {
          std::cerr << "minihls: --arg " << name << ": " << error << "\n";
          return false;
        }
        inputs.scalars[position] = *bits;
        continue;
      }
      if (symbol.kind == SymbolKind::Stream &&
          symbol.direction == minihls::sema::StreamDirection::Output) {
        std::cerr << "minihls: --arg " << name << ": '" << name
                  << "' is an output stream and takes no input\n";
        return false;
      }
      std::vector<std::uint64_t> values;
      for (const std::string_view element : split(text, ',')) {
        const auto bits = to_bits(element, symbol.type, error);
        if (!bits) {
          std::cerr << "minihls: --arg " << name << ": " << error << "\n";
          return false;
        }
        values.push_back(*bits);
      }
      if (symbol.kind == SymbolKind::Array) {
        if (values.size() > symbol.array_size) {
          std::cerr << "minihls: --arg " << name << ": " << values.size()
                    << " values given for an array of " << symbol.array_size << "\n";
          return false;
        }
        values.resize(symbol.array_size, 0);
        inputs.arrays[position] = std::move(values);
      } else {
        inputs.streams[position] = std::move(values);
      }
    }
    if (!found) {
      std::cerr << "minihls: --arg " << name << ": '" << function.function->name
                << "' has no parameter named '" << name << "'\n";
      return false;
    }
  }

  for (const int index : function.params) {
    const auto& symbol = function.symbols[static_cast<std::size_t>(index)];
    const auto position = static_cast<std::size_t>(symbol.param_index);
    if (symbol.kind == SymbolKind::Array && inputs.arrays[position].empty()) {
      inputs.arrays[position].assign(symbol.array_size, 0);
    }
    if (symbol.kind == SymbolKind::Scalar && !given[position]) {
      std::cerr << "minihls: missing --arg " << symbol.name << "=<value>\n";
      return false;
    }
  }
  return true;
}

// Prints the return value, every array the call modified, and stream traffic.
void print_outputs(const minihls::sema::FunctionInfo& function,
                   const minihls::interp::Inputs& inputs,
                   const minihls::interp::Outputs& outputs) {
  using minihls::sema::SymbolKind;
  if (outputs.return_value) {
    std::cout << "return " << minihls::sema::to_string(function.return_type) << " "
              << minihls::value_to_string(*outputs.return_value, function.return_type.width,
                                          function.return_type.is_signed)
              << "\n";
  }
  for (const int index : function.params) {
    const auto& symbol = function.symbols[static_cast<std::size_t>(index)];
    const auto position = static_cast<std::size_t>(symbol.param_index);
    if (symbol.kind == SymbolKind::Array && !symbol.is_const &&
        outputs.arrays[position] != inputs.arrays[position]) {
      std::cout << symbol.name << " " << format_list(outputs.arrays[position], symbol.type)
                << "\n";
    } else if (symbol.kind == SymbolKind::Stream) {
      if (symbol.direction == minihls::sema::StreamDirection::Output) {
        std::cout << symbol.name << " wrote "
                  << format_list(outputs.streams[position], symbol.type) << "\n";
      } else {
        std::cout << symbol.name << " consumed " << outputs.consumed[position] << "\n";
      }
    }
  }
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

  Options options;
  options.command = args.front();
  if (options.command != "parse" && options.command != "check" && options.command != "run" &&
      options.command != "emit-mlir") {
    return fail_usage("unknown command '" + options.command + "'");
  }

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];
    // The value following a flag, if there is one.
    auto next = [&]() -> std::optional<std::string> {
      if (i + 1 >= args.size()) return std::nullopt;
      return args[++i];
    };
    if (arg == "--print-ast") {
      options.print_ast = true;
    } else if (arg == "--report-truncations") {
      options.report_truncations = true;
    } else if (arg == "--optimize") {
      options.optimize = true;
    } else if (arg == "--report") {
      const auto value = next();
      if (!value) return fail_usage("--report needs a report name");
      if (*value != "ops") return fail_usage("unknown report '" + *value + "'");
      options.reports.push_back(*value);
    } else if (arg == "--top") {
      const auto value = next();
      if (!value) return fail_usage("--top needs a function name");
      options.top = *value;
    } else if (arg == "--arg") {
      const auto value = next();
      const std::size_t equals = value ? value->find('=') : std::string::npos;
      if (!value || equals == std::string::npos) {
        return fail_usage("--arg needs the form <name>=<value>");
      }
      options.args.emplace_back(value->substr(0, equals), value->substr(equals + 1));
    } else if (arg.starts_with("-")) {
      return fail_usage("unknown option '" + arg + "'");
    } else if (options.path.empty()) {
      options.path = arg;
    } else {
      return fail_usage("more than one input file given");
    }
  }
  if (options.path.empty()) return fail_usage("no input file given");
  if ((options.command == "run" || options.command == "emit-mlir") && options.top.empty()) {
    return fail_usage(options.command + " needs --top <function>");
  }

  std::ifstream stream(options.path, std::ios::binary);
  if (!stream) {
    std::cerr << "minihls: cannot open '" << options.path << "'\n";
    return 1;
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();

  minihls::SourceFile file(options.path, buffer.str());
  minihls::DiagnosticEngine diagnostics(file);
  const auto program = minihls::parse(file, diagnostics);

  // Diagnostics go to stderr so that printed output stays pipeable.
  auto report_errors = [&] {
    std::cerr << diagnostics.format_all();
    if (diagnostics.has_errors()) {
      std::cerr << "minihls: " << diagnostics.error_count() << " error(s) in '"
                << options.path << "'\n";
      return true;
    }
    return false;
  };

  if (options.command == "parse") {
    if (report_errors()) return 1;
    if (options.print_ast) std::cout << minihls::ast::print(*program);
    return 0;
  }

  minihls::sema::CheckedProgram checked;
  if (!diagnostics.has_errors()) minihls::sema::check(*program, diagnostics, checked);
  if (report_errors()) return 1;

  if (options.command == "check") {
    if (options.report_truncations) {
      for (const auto& function : checked.functions) {
        for (const auto& note : function.truncations) std::cout << diagnostics.format(note);
      }
    }
    return 0;
  }

  const minihls::sema::FunctionInfo* function = checked.find(options.top);
  if (function == nullptr) {
    std::cerr << "minihls: no function named '" << options.top << "' in '" << options.path
              << "'\n";
    return 1;
  }
  if (options.command == "emit-mlir") {
#ifdef MINIHLS_HAVE_MLIR
    mlir::MLIRContext context;
    const auto module = minihls::mlirgen::generate(context, *function, file, diagnostics);
    if (report_errors() || !module) return 1;

    const bool wants_ops =
        std::find(options.reports.begin(), options.reports.end(), "ops") != options.reports.end();
    // Measured before the pipeline runs, since it rewrites the module in place.
    const auto before = wants_ops ? minihls::transforms::measure(*module)
                                  : minihls::transforms::Metrics{};
    if (options.optimize && !minihls::transforms::optimize(*module)) {
      std::cerr << "minihls: the optimization pipeline failed\n";
      return 1;
    }
    module->print(llvm::outs());
    llvm::outs() << "\n";
    if (wants_ops) {
      // The report goes to stderr so that stdout stays a valid .mlir file.
      std::cerr << "\n"
                << minihls::transforms::format_report(before,
                                                      minihls::transforms::measure(*module));
    }
    return 0;
#else
    std::cerr << "minihls: this build has no MLIR support; see MINIHLS_MLIR in the README\n";
    return 1;
#endif
  }

  minihls::interp::Inputs inputs;
  if (!build_inputs(*function, options, inputs)) return 1;
  const minihls::interp::Outputs outputs = minihls::interp::run_ast(*function, inputs);
  if (!outputs.ok()) {
    std::cerr << "minihls: runtime error: " << outputs.error << "\n";
    return 1;
  }
  print_outputs(*function, inputs, outputs);
  return 0;
}
