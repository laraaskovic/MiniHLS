#pragma once
#include "frontend/ast.hpp"
#include "interp/interpreter.hpp"
#include <string>
#include <vector>

namespace minihls {

// One item on a line: a scalar, or a bracketed list. Shared with `run`,
// whose arguments have exactly the same shape — they just arrive in argv.
struct Item {
  bool isList = false;
  i128 scalar = 0;
  std::vector<i128> list;
};

// Splits "1 2 [3 4] 5" into items. Returns false on malformed brackets.
bool parseItems(const std::string& text, std::vector<Item>& out);

// Binds items to the function's parameters, in order; out-streams take none.
// `where` prefixes any error message ("file:12: " or "minihls run: ").
bool bindParams(const std::vector<Item>& items, const Function& fn,
                const std::string& where, std::vector<ParamValue>& out,
                std::string& error);

struct TestCase {
  unsigned line = 0;
  std::string text;                            // the original line, for messages
  std::vector<ParamValue> inputs;              // in parameter order
  std::vector<std::vector<i128>> expectedStreams;
  i128 expectedReturn = 0;
};

// Reads <stem>.tests and binds each case's values to the function's parameters.
bool loadTests(const std::string& path, const Function& fn,
               std::vector<TestCase>& out, std::string& error);

} // namespace minihls
