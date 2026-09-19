#include "interp/random_inputs.hpp"

#include "support/bits.hpp"

namespace minihls::interp {

std::uint64_t random_value(sema::IntType type, std::mt19937_64& rng) {
  const unsigned width = type.width;
  const std::uint64_t mask = mask_for_width(width);
  // One value in four is an edge case.
  switch (rng() % 16) {
    case 0:
      return 0;
    case 1:
      return 1 & mask;
    case 2:
      return mask;  // all ones: -1 or the unsigned maximum
    case 3:
      // The most negative signed value, or the unsigned value just past the
      // signed range.
      return (std::uint64_t{1} << (width - 1)) & mask;
    default:
      return rng() & mask;
  }
}

Inputs random_inputs(const sema::FunctionInfo& function, std::mt19937_64& rng,
                     std::size_t stream_length) {
  const std::size_t count = function.params.size();
  Inputs inputs;
  inputs.scalars.assign(count, 0);
  inputs.arrays.assign(count, {});
  inputs.streams.assign(count, {});
  for (const int index : function.params) {
    const sema::Symbol& s = function.symbols[static_cast<std::size_t>(index)];
    const auto position = static_cast<std::size_t>(s.param_index);
    switch (s.kind) {
      case sema::SymbolKind::Scalar:
        inputs.scalars[position] = random_value(s.type, rng);
        break;
      case sema::SymbolKind::Array:
        for (std::uint64_t k = 0; k < s.array_size; ++k) {
          inputs.arrays[position].push_back(random_value(s.type, rng));
        }
        break;
      case sema::SymbolKind::Stream:
        if (s.direction != sema::StreamDirection::Output) {
          for (std::size_t k = 0; k < stream_length; ++k) {
            inputs.streams[position].push_back(random_value(s.type, rng));
          }
        }
        break;
    }
  }
  return inputs;
}

}  // namespace minihls::interp
