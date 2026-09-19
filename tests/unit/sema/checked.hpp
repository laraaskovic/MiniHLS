#pragma once

#include "frontend/diagnostics.hpp"
#include "frontend/parser.hpp"
#include "frontend/source.hpp"
#include "sema/sema.hpp"

#include <memory>
#include <string>

namespace minihls::testing {

// Parses and checks a program, keeping the source file alive for as long as
// the AST and diagnostics that refer to it.
class Checked {
 public:
  explicit Checked(std::string text)
      : file_(std::make_unique<SourceFile>("test.hc", std::move(text))),
        diagnostics_(std::make_unique<DiagnosticEngine>(*file_)),
        program_(parse(*file_, *diagnostics_)) {
    if (!diagnostics_->has_errors()) {
      ok_ = sema::check(*program_, *diagnostics_, checked_);
    }
  }

  bool ok() const { return ok_; }
  const DiagnosticEngine& diagnostics() const { return *diagnostics_; }
  std::string messages() const { return diagnostics_->format_all(); }
  bool mentions(std::string_view text) const { return diagnostics_->contains(text); }
  const sema::FunctionInfo& function(std::size_t index = 0) const {
    return checked_.functions.at(index);
  }
  const ast::Program& program() const { return *program_; }
  const SourceFile& file() const { return *file_; }
  DiagnosticEngine& mutable_diagnostics() { return *diagnostics_; }

 private:
  std::unique_ptr<SourceFile> file_;
  std::unique_ptr<DiagnosticEngine> diagnostics_;
  std::unique_ptr<ast::Program> program_;
  sema::CheckedProgram checked_;
  bool ok_ = false;
};

}  // namespace minihls::testing
