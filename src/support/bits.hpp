#pragma once
#include <cstdint>
#include <string>

namespace minihls {

// __extension__ silences -Wpedantic for exactly these two declarations.
// __int128 is not ISO C++, which is true and which we have decided to
// accept: the language computes to 128 bits, so the interpreter needs a
// 128-bit integer, and there is no portable one. Narrowing the warning
// here keeps -Wpedantic useful everywhere else instead of training us to
// ignore it.
__extension__ using u128 = unsigned __int128;
__extension__ using i128 = __int128;

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

// Decimal rendering, honouring `isSigned`. Works for the full 128-bit range.
std::string toString(const Bits& b);

enum class Cmp { Lt, Le, Gt, Ge, Eq, Ne };
Bits compare(Bits a, Bits b, Cmp op);

} // namespace minihls