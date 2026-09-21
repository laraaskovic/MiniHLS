#include "driver/compile.hpp"
#include "driver/testfile.hpp"
#include "interp/interpreter.hpp"
#include <iostream>

namespace minihls {
namespace {

// Compares through toString rather than raw bits, so a width mismatch shows
// up as a readable difference instead of two hex blobs.
bool sameValue(const Bits& got, i128 expected) {
  return toString(got) == toString(Bits::make(static_cast<u128>(expected),
                                              got.width, got.isSigned));
}

// Everything up to and including the front end. Returns false once it has
// printed whatever went wrong.
bool frontEnd(const std::string& sourcePath, Compilation& c) {
  c = compileFile(sourcePath);
  if (!c.source) return false;
  c.diags->print(std::cerr);
  return c.ok;
}

std::string streamText(const std::vector<Bits>& values) {
  std::string text = "[";
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) text += ' ';
    text += toString(values[i]);
  }
  return text + "]";
}

} // namespace

int runTests(const std::string& sourcePath) {
  Compilation c;
  if (!frontEnd(sourcePath, c)) return 1;

  std::string stem = sourcePath;
  if (auto dot = stem.rfind('.'); dot != std::string::npos) stem.erase(dot);

  std::vector<TestCase> cases;
  std::string error;
  if (!loadTests(stem + ".tests", c.program.fn, cases, error)) {
    std::cerr << "minihls: " << error << "\n";
    return 1;
  }

  unsigned passed = 0, failed = 0;
  for (const TestCase& t : cases) {
    // Diagnostics accumulate across cases, so report only what this run added.
    size_t before = c.diags->all().size();
    auto result = Interpreter(*c.diags).run(c.program, t.inputs);
    if (!result) {
      std::cerr << stem << ".tests:" << t.line << ": FAILED to run\n";
      for (size_t i = before; i < c.diags->all().size(); ++i)
        std::cerr << "    " << c.diags->all()[i].message << "\n";
      ++failed; continue;
    }

    bool ok = sameValue(result->returnValue, t.expectedReturn);

    // Out-streams, in order.
    for (size_t s = 0; ok && s < t.expectedStreams.size(); ++s) {
      if (s >= result->streamOutputs.size() ||
          result->streamOutputs[s].size() != t.expectedStreams[s].size()) { ok = false; break; }
      for (size_t k = 0; k < t.expectedStreams[s].size(); ++k)
        if (!sameValue(result->streamOutputs[s][k], t.expectedStreams[s][k])) { ok = false; break; }
    }

    if (ok) { ++passed; continue; }
    ++failed;
    std::cerr << stem << ".tests:" << t.line << ": FAILED\n"
              << "    " << t.text << "\n"
              << "    got return " << toString(result->returnValue) << "\n";
    for (const std::vector<Bits>& out : result->streamOutputs)
      std::cerr << "    got stream " << streamText(out) << "\n";
  }

  std::cout << sourcePath << ": " << passed << " passed, " << failed << " failed\n";
  return failed == 0 ? 0 : 1;
}

// Same binding loop as a test case; the values come from argv instead of a
// file, so the arguments are joined back into one line and parsed as items.
int runProgram(const std::string& sourcePath, const std::vector<std::string>& args) {
  Compilation c;
  if (!frontEnd(sourcePath, c)) return 1;

  std::string line;
  for (const std::string& a : args) {
    if (!line.empty()) line += ' ';
    line += a;
  }

  std::vector<Item> items;
  if (!parseItems(line, items)) {
    std::cerr << "minihls run: malformed values\n";
    return 1;
  }

  std::vector<ParamValue> inputs;
  std::string error;
  if (!bindParams(items, c.program.fn, "minihls run: ", inputs, error)) {
    std::cerr << error << "\n";
    return 1;
  }

  auto result = Interpreter(*c.diags).run(c.program, inputs);
  if (!result) {
    c.diags->print(std::cerr);
    std::cerr << "minihls run: execution failed\n";
    return 1;
  }

  // Out-streams first, then the returned value — the .tests line order.
  for (const std::vector<Bits>& out : result->streamOutputs)
    std::cout << streamText(out) << "\n";
  std::cout << toString(result->returnValue) << "\n";
  return 0;
}

} // namespace minihls
