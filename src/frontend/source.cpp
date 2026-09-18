#include "frontend/source.hpp"

#include <algorithm>
#include <utility>

namespace minihls {

SourceRange join(SourceRange first, SourceRange second) {
  return SourceRange{
      SourceLocation{std::min(first.begin.offset, second.begin.offset)},
      SourceLocation{std::max(first.end.offset, second.end.offset)},
  };
}

SourceFile::SourceFile(std::string path, std::string contents)
    : path_(std::move(path)), contents_(std::move(contents)) {
  line_starts_.push_back(0);
  for (std::size_t i = 0; i < contents_.size(); ++i) {
    if (contents_[i] == '\n') {
      line_starts_.push_back(static_cast<std::uint32_t>(i + 1));
    }
  }
}

LineColumn SourceFile::resolve(SourceLocation location) const {
  const auto offset =
      std::min<std::uint32_t>(location.offset, static_cast<std::uint32_t>(contents_.size()));
  // The first line start strictly greater than offset; the line we want is the
  // one before it.
  const auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
  const auto index = static_cast<std::uint32_t>(std::distance(line_starts_.begin(), it) - 1);
  return LineColumn{index + 1, offset - line_starts_[index] + 1};
}

std::string_view SourceFile::line_text(std::uint32_t line) const {
  if (line == 0 || line > line_starts_.size()) return {};
  const std::uint32_t start = line_starts_[line - 1];
  const std::uint32_t end = line < line_starts_.size()
                                ? line_starts_[line]
                                : static_cast<std::uint32_t>(contents_.size());
  std::string_view text(contents_.data() + start, end - start);
  // Trim the line terminator, handling CRLF as well as LF.
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  return text;
}

std::string_view SourceFile::text_for(SourceRange range) const {
  const auto size = static_cast<std::uint32_t>(contents_.size());
  const std::uint32_t begin = std::min(range.begin.offset, size);
  const std::uint32_t end = std::clamp(range.end.offset, begin, size);
  return std::string_view(contents_.data() + begin, end - begin);
}

}  // namespace minihls
