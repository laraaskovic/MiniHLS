#include "frontend/diagnostics.hpp"

namespace minihls {

void Diagnostics::print(std::ostream& out) const {
  for (const Diagnostic& diagnostic : diags_) {
    out << src_.path() << ':' << src_.line(diagnostic.range.begin)
        << ':' << src_.column(diagnostic.range.begin) << ": error: "
        << diagnostic.message << '\n';
    out << src_.lineText(src_.line(diagnostic.range.begin)) << '\n';
  }
}

} // namespace minihls