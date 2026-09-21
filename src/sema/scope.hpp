// src/sema/scope.hpp
#pragma once

#include "sema/symbol.hpp"
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace minihls {
class ScopeStack {
public:
  void push() { scopes_.emplace_back(); }
  void pop()  { scopes_.pop_back(); }

  // innermost outward — this is what makes shadowing work
  Symbol* lookup(std::string_view name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
      auto found = it->find(std::string(name));
      if (found != it->end()) return found->second;
    }
    return nullptr;
  }

  // false if already declared in THIS scope (shadowing an outer one is legal)
  bool declare(Symbol* s) {
    return scopes_.back().emplace(s->name, s).second;
  }

private:
  std::vector<std::unordered_map<std::string, Symbol*>> scopes_;
};

} // namespace minihls