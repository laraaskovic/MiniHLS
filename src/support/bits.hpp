#pragma once
#include <cstdint>

namespace minihls {

using u128 = unsigned __int128;
using i128 = __int128;

// A value of one of the language's integer types: `width` meaningful bits,
// interpreted per `isSigned`. Bits above `width` in `raw` are always zero.
struct Bits {
  u128 raw = 0;
  unsigned width = 1;
  bool isSigned = false;

  // The only constructor: truncates to `width` so `raw` is always canonical.
  static Bits make(u128 rawBits, unsigned width, bool isSigned);

  // The mathematical value, sign-extended if this is a signed type.
  i128 value() const;
};

u128 mask(unsigned width);        // low `width` bits set

Bits castTo(Bits v, unsigned width, bool isSigned);   // the language's T(e)

Bits add(Bits a, Bits b);
Bits sub(Bits a, Bits b);
Bits mul(Bits a, Bits b);
Bits divide(Bits a, Bits b);
Bits remainder(Bits a, Bits b);
Bits shl(Bits a, Bits b);
Bits shr(Bits a, Bits b);
Bits bitAnd(Bits a, Bits b);
Bits bitOr(Bits a, Bits b);
Bits bitXor(Bits a, Bits b);
Bits neg(Bits a);
Bits bitNot(Bits a);

enum class Cmp { Lt, Le, Gt, Ge, Eq, Ne };
Bits compare(Bits a, Bits b, Cmp op);

} // namespace minihls