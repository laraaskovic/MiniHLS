#pragma once

#include "frontend/ast.hpp"
#include <cstdint>
#include <string>

namespace minihls {
enum class SymbolKind {
  ScalarParam, ArrayParam, StreamParam,   // function parameters
  Local, LocalArray,                       // declared in the body
  GlobalConst, GlobalConstArray,           // file scope
  InductionVar,                            // a for-loop variable
};

struct Symbol {
  SymbolKind kind;
  std::string name;
  Type type;                  // element type for arrays and streams
  Range declRange;            // where it was declared — for "previously declared here"
  bool isStreamOut = false;   // StreamParam only
  uint64_t arrayLength = 0;   // arrays only, filled by S3
};

} // namespace minihls