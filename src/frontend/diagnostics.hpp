#pragma once
#include "frontend/source.hpp"
#include <ostream>
#include <string>
#include <vector>

namespace minihls {

struct Diagnostic { Range range; std::string message; };

class Diagnostics {
public:
  explicit Diagnostics(const SourceFile& src) : src_(src) {}
  void error(Range r, std::string message) { diags_.push_back({r, std::move(message)}); }
  bool hasErrors() const { return !diags_.empty(); }
  const std::vector<Diagnostic>& all() const { return diags_; }
  void print(std::ostream&) const;     // the caret format
private:
  const SourceFile& src_;
  std::vector<Diagnostic> diags_;
};

} // namespace minihls