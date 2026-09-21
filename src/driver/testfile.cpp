#include "driver/testfile.hpp"
#include <fstream>
#include <sstream>

namespace minihls {
namespace {

bool parseInt(const std::string& text, i128& out) {
  if (text.empty()) return false;
  size_t i = 0;
  bool negative = text[0] == '-';
  if (negative || text[0] == '+') i = 1;
  if (i >= text.size()) return false;
  u128 magnitude = 0;
  for (; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9') return false;
    magnitude = magnitude * 10 + static_cast<unsigned>(text[i] - '0');
  }
  out = negative ? -static_cast<i128>(magnitude) : static_cast<i128>(magnitude);
  return true;
}

Bits make(i128 v, Type t) {
  return Bits::make(static_cast<u128>(v), t.width, t.isSigned);
}

} // namespace

bool parseItems(const std::string& text, std::vector<Item>& out) {
  std::istringstream in(text);
  std::string word;
  while (in >> word) {
    if (word.front() == '[') {
      Item item; item.isList = true;
      word.erase(0, 1);                         // drop '['
      for (;;) {
        bool last = !word.empty() && word.back() == ']';
        if (last) word.pop_back();
        if (!word.empty()) {
          i128 v;
          if (!parseInt(word, v)) return false;
          item.list.push_back(v);
        }
        if (last) break;
        if (!(in >> word)) return false;        // unterminated '['
      }
      out.push_back(std::move(item));
    } else {
      Item item;
      if (!parseInt(word, item.scalar)) return false;
      out.push_back(std::move(item));
    }
  }
  return true;
}

bool bindParams(const std::vector<Item>& items, const Function& fn,
                const std::string& where, std::vector<ParamValue>& out,
                std::string& error) {
  size_t next = 0;
  for (const Param& p : fn.params) {
    if (p.kind == ParamKind::StreamOut) { out.push_back({}); continue; }
    if (next >= items.size()) { error = where + "too few inputs"; return false; }
    const Item& item = items[next++];
    ParamValue value;
    if (p.kind == ParamKind::Scalar) {
      if (item.isList) { error = where + "expected a scalar for '" + p.name + "'"; return false; }
      value.scalar = make(item.scalar, p.type);
    } else {
      if (!item.isList) { error = where + "expected a list for '" + p.name + "'"; return false; }
      for (i128 v : item.list) value.elements.push_back(make(v, p.type));
    }
    out.push_back(std::move(value));
  }
  if (next != items.size()) { error = where + "too many inputs"; return false; }
  return true;
}

bool loadTests(const std::string& path, const Function& fn,
               std::vector<TestCase>& out, std::string& error) {
  std::ifstream file(path);
  if (!file) { error = "cannot open " + path; return false; }

  std::string line;
  unsigned lineNo = 0;
  while (std::getline(file, line)) {
    ++lineNo;
    if (auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    if (line.find_first_not_of(" \t\r") == std::string::npos) continue;

    std::string where = path + ":" + std::to_string(lineNo) + ": ";

    auto arrow = line.find("->");
    if (arrow == std::string::npos) { error = where + "missing '->'"; return false; }

    std::vector<Item> lhs, rhs;
    if (!parseItems(line.substr(0, arrow), lhs) ||
        !parseItems(line.substr(arrow + 2), rhs)) {
      error = where + "malformed values";
      return false;
    }

    TestCase testCase;
    testCase.line = lineNo;
    testCase.text = line;

    // Bind left-hand items to parameters, in order. Out-streams take none.
    if (!bindParams(lhs, fn, where, testCase.inputs, error)) return false;

    // Right-hand side: lists are out-streams in order, then the return value.
    if (rhs.empty() || rhs.back().isList) {
      error = where + "expected a return value";
      return false;
    }
    for (size_t i = 0; i + 1 < rhs.size(); ++i) {
      if (!rhs[i].isList) { error = where + "expected a stream list"; return false; }
      testCase.expectedStreams.push_back(rhs[i].list);
    }
    testCase.expectedReturn = rhs.back().scalar;

    out.push_back(std::move(testCase));
  }
  return true;
}

} // namespace minihls
