// The milestone 1 done-criterion: every example parses, and print-then-reparse
// is stable.
//
// Stability is a property test rather than a table of expected output. Parse a
// file, print it, parse the printed text, and print that: the two printed forms
// must be byte-identical. Any parser bug that loses structure, or printer bug
// that emits something the parser reads differently, shows up as a mismatch --
// without anyone maintaining golden files by hand.

#include "frontend/ast_printer.hpp"
#include "frontend/diagnostics.hpp"
#include "frontend/parser.hpp"
#include "frontend/source.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using minihls::DiagnosticEngine;
using minihls::SourceFile;
namespace ast = minihls::ast;

std::string read_file(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

struct ParseOutcome {
  std::string printed;
  std::string errors;
  bool ok = false;
};

ParseOutcome parse_and_print(const std::string& name, std::string text) {
  SourceFile file(name, std::move(text));
  DiagnosticEngine diagnostics(file);
  const auto program = minihls::parse(file, diagnostics);
  ParseOutcome outcome;
  outcome.ok = !diagnostics.has_errors();
  outcome.errors = diagnostics.format_all();
  if (outcome.ok) outcome.printed = ast::print(*program);
  return outcome;
}

std::vector<std::filesystem::path> example_files() {
  std::vector<std::filesystem::path> paths;
  const std::filesystem::path directory(MINIHLS_EXAMPLES_DIR);
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.is_regular_file() && entry.path().extension() == ".hc") {
      paths.push_back(entry.path());
    }
  }
  // directory_iterator order is unspecified; sort so failures are reproducible.
  std::sort(paths.begin(), paths.end());
  return paths;
}

TEST(Examples, DirectoryIsNotEmpty) {
  // Guards against the round-trip test silently passing because it found no
  // files -- a vacuous green test is worse than a red one.
  EXPECT_GE(example_files().size(), 4u) << "looked in " << MINIHLS_EXAMPLES_DIR;
}

TEST(Examples, EveryExampleParsesCleanly) {
  for (const auto& path : example_files()) {
    const ParseOutcome outcome = parse_and_print(path.filename().string(), read_file(path));
    EXPECT_TRUE(outcome.ok) << path.filename().string() << " failed to parse:\n"
                            << outcome.errors;
  }
}

TEST(Examples, PrintThenReparseIsStable) {
  for (const auto& path : example_files()) {
    const std::string name = path.filename().string();
    const ParseOutcome first = parse_and_print(name, read_file(path));
    ASSERT_TRUE(first.ok) << name << ":\n" << first.errors;

    const ParseOutcome second = parse_and_print(name + " (reprinted)", first.printed);
    ASSERT_TRUE(second.ok) << name << " printed output did not reparse:\n"
                           << second.errors << "\n--- printed ---\n"
                           << first.printed;

    EXPECT_EQ(first.printed, second.printed)
        << name << " is not stable under print-then-reparse";
  }
}

// The same property, over constructs chosen to stress the printer's
// parenthesization rather than whatever the examples happen to contain.
TEST(RoundTrip, IsStableForTrickyExpressions) {
  const std::vector<std::string> sources = {
      "i32 f(i32 a, i32 b, i32 c) { return a - (b - c); }",
      "i32 f(i32 a, i32 b, i32 c) { return (a + b) * c; }",
      "i32 f(i32 a, i32 b, i32 c) { return a ? b : c ? a : b; }",
      "i32 f(i32 a, i32 b, i32 c) { return (a ? b : c) ? a : b; }",
      "i32 f(i32 a) { return -(-a); }",
      "i32 f(i32 a, i32 b) { return ~(a & b) | (a ^ b); }",
      "i32 f(i32 a, i32 b) { return (a << 2) + (b >> 1); }",
      "i32 f(i16 v[8], i32 i) { return v[i + 1] * v[i - 1]; }",
      "i8 f(i8 p, i8 q) { if (p) if (q) return 1; else return 2; return 3; }",
      "i8 f() { const i8 c[3] = { 1, -2, 3 }; return c[0]; }",
      "void f(stream<u8> in, stream<u8> out) { write(out, read(in)); }",
      "i8 f() { for (u4 i = 0; i < 8; i = i + 1) { #pragma unroll factor=2\n } return 0; }",
      "i8 f(i8 a) { i8 t = 0; if (a) t = 1; else t = 2; return t; }",
  };

  for (const std::string& source : sources) {
    const ParseOutcome first = parse_and_print("inline.hc", source);
    ASSERT_TRUE(first.ok) << source << "\n" << first.errors;

    const ParseOutcome second = parse_and_print("inline.hc", first.printed);
    ASSERT_TRUE(second.ok) << "printed output did not reparse:\n"
                           << first.printed << "\n" << second.errors;

    EXPECT_EQ(first.printed, second.printed) << "unstable for: " << source;
  }
}

}  // namespace
