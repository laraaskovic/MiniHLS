#include "frontend/diagnostics.hpp"

#include <utility>

namespace minihls {

std::string_view severity_name(Severity severity) {
  switch (severity) {
    case Severity::Error:
      return "error";
    case Severity::Warning:
      return "warning";
    case Severity::Note:
      return "note";
  }
  return "error";
}

void DiagnosticEngine::add(Severity severity, SourceRange range, std::string message) {
  diagnostics_.push_back(Diagnostic{severity, range, std::move(message)});
  if (severity == Severity::Error) ++error_count_;
}

void DiagnosticEngine::error(SourceRange range, std::string message) {
  add(Severity::Error, range, std::move(message));
}

void DiagnosticEngine::warning(SourceRange range, std::string message) {
  add(Severity::Warning, range, std::move(message));
}

void DiagnosticEngine::note(SourceRange range, std::string message) {
  add(Severity::Note, range, std::move(message));
}

std::string DiagnosticEngine::format(const Diagnostic& diagnostic) const {
  const LineColumn start = file_->resolve(diagnostic.range.begin);
  const std::string_view line = file_->line_text(start.line);

  std::string out;
  out += file_->path();
  out += ':';
  out += std::to_string(start.line);
  out += ':';
  out += std::to_string(start.column);
  out += ": ";
  out += severity_name(diagnostic.severity);
  out += ": ";
  out += diagnostic.message;
  out += '\n';

  out += "    ";
  out += line;
  out += "\n    ";

  // Indent to the column, expanding tabs as single spaces so the caret lands
  // under the right character in the (re-indented) copy printed above.
  for (std::uint32_t column = 1; column < start.column; ++column) {
    out += ' ';
  }

  // Underline the range, but never past the end of this line and always at
  // least one character so a zero-width range still points somewhere.
  const LineColumn end = file_->resolve(diagnostic.range.end);
  std::uint32_t width = 1;
  if (end.line == start.line && end.column > start.column) {
    width = end.column - start.column;
  }
  out += '^';
  for (std::uint32_t i = 1; i < width; ++i) out += '~';
  out += '\n';
  return out;
}

std::string DiagnosticEngine::format_all() const {
  std::string out;
  for (const Diagnostic& diagnostic : diagnostics_) {
    out += format(diagnostic);
  }
  return out;
}

bool DiagnosticEngine::contains(std::string_view needle) const {
  for (const Diagnostic& diagnostic : diagnostics_) {
    if (diagnostic.message.find(needle) != std::string::npos) return true;
  }
  return false;
}

}  // namespace minihls
