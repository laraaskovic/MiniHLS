#pragma once

#include "frontend/source.hpp"

#include <string>
#include <vector>

namespace minihls {

enum class Severity { Error, Warning, Note };

std::string_view severity_name(Severity severity);

struct Diagnostic {
  Severity severity = Severity::Error;
  SourceRange range;
  std::string message;
};

// Collects diagnostics rather than printing them as they occur, so that a caller
// can decide whether to report, count, or discard them. Tests rely on this:
// they assert on messages without capturing stderr.
class DiagnosticEngine {
 public:
  explicit DiagnosticEngine(const SourceFile& file) : file_(&file) {}

  void error(SourceRange range, std::string message);
  void warning(SourceRange range, std::string message);
  void note(SourceRange range, std::string message);

  bool has_errors() const { return error_count_ > 0; }
  std::size_t error_count() const { return error_count_; }
  const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

  // Renders one diagnostic as `path:line:col: severity: message`, followed by
  // the offending source line and a caret underlining the range.
  std::string format(const Diagnostic& diagnostic) const;

  // Renders every diagnostic in the order they were reported.
  std::string format_all() const;

  // True if any diagnostic message contains `needle`. Test helper.
  bool contains(std::string_view needle) const;

 private:
  void add(Severity severity, SourceRange range, std::string message);

  const SourceFile* file_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t error_count_ = 0;
};

}  // namespace minihls
