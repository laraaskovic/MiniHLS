#include "interp/io.hpp"

namespace minihls::interp {
namespace {

std::string compare_lists(const char* what, const std::vector<std::vector<std::uint64_t>>& a,
                          const std::vector<std::vector<std::uint64_t>>& b) {
  if (a.size() != b.size()) return std::string(what) + ": different parameter counts";
  for (std::size_t p = 0; p < a.size(); ++p) {
    if (a[p].size() != b[p].size()) {
      return std::string(what) + " of parameter " + std::to_string(p) + ": length " +
             std::to_string(a[p].size()) + " vs " + std::to_string(b[p].size());
    }
    for (std::size_t i = 0; i < a[p].size(); ++i) {
      if (a[p][i] != b[p][i]) {
        return std::string(what) + " of parameter " + std::to_string(p) + " element " +
               std::to_string(i) + ": " + std::to_string(a[p][i]) + " vs " +
               std::to_string(b[p][i]);
      }
    }
  }
  return {};
}

}  // namespace

std::string compare(const Outputs& expected, const Outputs& actual) {
  if (expected.error != actual.error) {
    return "error: '" + expected.error + "' vs '" + actual.error + "'";
  }
  if (!expected.ok()) return {};
  if (expected.return_value != actual.return_value) {
    auto show = [](const std::optional<std::uint64_t>& v) {
      return v ? std::to_string(*v) : std::string("none");
    };
    return "return value: " + show(expected.return_value) + " vs " +
           show(actual.return_value);
  }
  if (auto d = compare_lists("array", expected.arrays, actual.arrays); !d.empty()) return d;
  if (auto d = compare_lists("stream", expected.streams, actual.streams); !d.empty()) return d;
  if (expected.consumed != actual.consumed) return "stream elements consumed differ";
  return {};
}

}  // namespace minihls::interp
