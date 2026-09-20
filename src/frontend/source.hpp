#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace minihls {

struct Loc { uint32_t offset = 0; };
struct Range { Loc begin, end; };

// Owns a file's text and converts byte offsets into line/column.
class SourceFile {
public:
  SourceFile(std::string path, std::string text);
  static std::optional<SourceFile> load(const std::string& path);

  const std::string& path() const { return path_; }
  const std::string& text() const { return text_; }

  unsigned line(Loc) const;              // 1-based
  unsigned column(Loc) const;            // 1-based
  std::string_view lineText(unsigned line) const;

private:
  std::string path_, text_;
  std::vector<uint32_t> lineStarts_;     // offset of each line's first byte
};

} // namespace minihls