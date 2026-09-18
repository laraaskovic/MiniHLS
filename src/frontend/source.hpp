#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace minihls {

// A byte offset into a SourceFile. Storing an offset rather than a line/column
// pair keeps tokens small; the line and column are recovered on demand, which
// only happens when a diagnostic is actually printed.
struct SourceLocation {
  std::uint32_t offset = 0;

  friend bool operator==(SourceLocation, SourceLocation) = default;
};

// A half-open byte range [begin, end).
struct SourceRange {
  SourceLocation begin;
  SourceLocation end;

  friend bool operator==(SourceRange, SourceRange) = default;
};

// Joins two ranges into one spanning both. Used to give a compound expression a
// range covering all of its operands.
SourceRange join(SourceRange first, SourceRange second);

// A 1-based line and column, for display only.
struct LineColumn {
  std::uint32_t line = 1;
  std::uint32_t column = 1;
};

// One file of source text, plus the line index needed to turn offsets back into
// line/column pairs.
class SourceFile {
 public:
  SourceFile(std::string path, std::string contents);

  const std::string& path() const { return path_; }
  const std::string& contents() const { return contents_; }

  // Resolves an offset to a 1-based line and column. Offsets past the end
  // clamp to the last position, so a diagnostic at end-of-file still prints.
  LineColumn resolve(SourceLocation location) const;

  // The text of a 1-based line, without its terminator.
  std::string_view line_text(std::uint32_t line) const;

  std::uint32_t line_count() const {
    return static_cast<std::uint32_t>(line_starts_.size());
  }

  // The text covered by a range, clamped to the file.
  std::string_view text_for(SourceRange range) const;

 private:
  std::string path_;
  std::string contents_;
  // Byte offset at which each line begins. Always has at least one entry.
  std::vector<std::uint32_t> line_starts_;
};

}  // namespace minihls
