#include "frontend/source.hpp"

#include <algorithm>
#include <fstream>

namespace minihls {

SourceFile::SourceFile(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)) {
  lineStarts_.push_back(0);
  for (uint32_t i = 0; i < text_.size(); ++i)
    if (text_[i] == '\n') lineStarts_.push_back(i + 1);
}

std::optional<SourceFile> SourceFile::load(const std::string& path) {
  std::ifstream input(path);
  if (!input) return std::nullopt;
  return SourceFile(path, std::string((std::istreambuf_iterator<char>(input)), {}));
}

unsigned SourceFile::line(Loc loc) const {
  auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), loc.offset);
  return static_cast<unsigned>(it - lineStarts_.begin());
}

unsigned SourceFile::column(Loc loc) const {
  unsigned currentLine = line(loc);
  return loc.offset - lineStarts_[currentLine - 1] + 1;
}

std::string_view SourceFile::lineText(unsigned lineNumber) const {
  if (lineNumber == 0 || lineNumber > lineStarts_.size()) return {};
  uint32_t begin = lineStarts_[lineNumber - 1];
  uint32_t end = lineNumber < lineStarts_.size()
                   ? lineStarts_[lineNumber] : static_cast<uint32_t>(text_.size());
  if (end > begin && text_[end - 1] == '\n') --end;
  if (end > begin && text_[end - 1] == '\r') --end;
  return std::string_view(text_).substr(begin, end - begin);
}

} // namespace minihls