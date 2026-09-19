#pragma once

#include "interp/io.hpp"
#include "sema/sema.hpp"

#include <cstdint>
#include <random>

namespace minihls::interp {

// Random inputs for a function: every scalar, every array element, and a
// supply of data for every input stream.
//
// Values are biased toward the edges of each type -- zero, one, all ones, the
// most negative and most positive values -- because that is where width and
// signedness bugs live, and uniform sampling of a wide type almost never hits
// them.
Inputs random_inputs(const sema::FunctionInfo& function, std::mt19937_64& rng,
                     std::size_t stream_length = 1024);

// One random value of the given type, as its bits.
std::uint64_t random_value(sema::IntType type, std::mt19937_64& rng);

}  // namespace minihls::interp
