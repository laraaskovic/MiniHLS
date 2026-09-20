#include "support/bits.hpp"

namespace minihls {

// helpers

u128 mask(unsigned width) {
  if (width == 0)   return 0;
  if (width >= 128) return ~static_cast<u128>(0);   // 1u128 << 128 is UB
  return (static_cast<u128>(1) << width) - 1;
}

Bits Bits::make(u128 rawBits, unsigned width, bool isSigned) {
  return Bits{ rawBits & mask(width), width, isSigned };
}

i128 Bits::value() const {
  // Signed and top bit set: set every bit above the width, so the 128-bit
  // reinterpretation is the right negative number.
  if (isSigned && ((raw >> (width - 1)) & 1))
    return static_cast<i128>(raw | ~mask(width));
  return static_cast<i128>(raw);
}

// Operands come in sign- or zero-extended to 128 bits, so mixed widths need
// no special handling anywhere below.
static u128 extended(Bits v) { return static_cast<u128>(v.value()); }

static unsigned maxw(Bits a, Bits b) { return a.width > b.width ? a.width : b.width; }
static unsigned minw(Bits a, Bits b) { return a.width < b.width ? a.width : b.width; }

// arithmetic

Bits add(Bits a, Bits b) {
  // width max(wa,wb)+1, signedness of the operands.

  // size is maxw(a, b) + 1, but the result is always signed if either operand is signed
  return Bits::make(static_cast<u128>(a.value() + b.value()),
                    maxw(a, b) + 1, a.isSigned || b.isSigned);
}

Bits shl(Bits a, Bits b) {
  // width wa, signedness of a. Runtime table: amount >= wa -> 0.
  // The guard must come first 
  unsigned amount = static_cast<unsigned>(b.raw);
  if (amount >= a.width) return Bits::make(0, a.width, a.isSigned);
  return Bits::make(a.raw << amount, a.width, a.isSigned);
}

// width max(wa,wb)+1, and the result is ALWAYS SIGNED, even when both
// operands are unsigned — u8(0) - u8(1) is i9(-1), not 511.
Bits sub(Bits a, Bits b) { 
    i128 result = a.value() - b.value();
    return Bits::make(
        static_cast<u128>(result),
        maxw(a, b) + 1,
        true);
}

// width wa+wb. Compute with value() so a signed product sign-extends.
Bits mul(Bits a, Bits b) { 
    u128 result = static_cast<u128>(a.value() * b.value());
    return Bits::make(result, a.width + b.width, a.isSigned || b.isSigned);
}

// width wa+1 if signed, wa if unsigned.
//   - b == 0 -> result 0 (runtime table).
//   - Compute through value() as i128: i8(-128)/i8(-1) is 128, which traps
//     if you do it in the narrow type but is fine at 128 bits.
//   - C++ integer division already truncates toward zero, which is what the
//     spec asks for.
//   - Unsigned operands at width 128: compare and divide raw as u128, since
//     value() would reinterpret a top-bit-set u128 as negative.
Bits divide(Bits a, Bits b) {
    // divide by 0 error returns 0
    if (b.raw == 0) return Bits::make(0, a.width, a.isSigned);

    // if signed return with +1 width otherwise unsigned
    if (a.isSigned || b.isSigned) {
        i128 result = a.value() / b.value();
        return Bits::make(static_cast<u128>(result), a.width + 1, true);
    } else {
        u128 result = a.raw / b.raw;
        return Bits::make(result, a.width, false);
    }
}

// width min(wa,wb). b == 0 -> 0. C++ % already gives the sign of the
// dividend, which matches the spec.
Bits remainder(Bits a, Bits b) {
    if (b.raw == 0) return Bits::make(0, minw(a, b), a.isSigned || b.isSigned);

    i128 result = a.value() % b.value();
    return Bits::make(static_cast<u128>(result), minw(a, b), a.isSigned || b.isSigned);
}

// width wa+1, always signed.
Bits neg(Bits a) { 
    u128 result = static_cast<u128>(-a.value());
    return Bits::make(result, a.width + 1, true);
}

// bitwise

// for the three below: width max(wa,wb), signedness of the operands.
// Use extended() for each operand so a narrower signed value sign-extends
// into the wider result before the operation.
Bits bitAnd(Bits a, Bits b) {
    u128 result = extended(a) & extended(b);
    return Bits::make(result, maxw(a, b), a.isSigned || b.isSigned);
}
Bits bitOr (Bits a, Bits b) {
    u128 result = extended(a) | extended(b);
    return Bits::make(result, maxw(a, b), a.isSigned || b.isSigned);

}
Bits bitXor(Bits a, Bits b) {
    u128 result = extended(a) ^ extended(b);
    return Bits::make(result, maxw(a, b), a.isSigned || b.isSigned);
}

// width wa, same signedness. ~raw, then truncate.
Bits bitNot(Bits a) {
    return Bits::make(~a.raw, a.width, a.isSigned);
}

// width wa, signedness of a.
//   - amount >= wa: unsigned -> 0; signed -> 0 if a >= 0, else all ones (-1).
//   - otherwise: signed -> arithmetic (shift value()), unsigned -> raw >> n.
Bits shr(Bits a, Bits b) {
    unsigned amount = static_cast<unsigned>(b.raw);
    if (amount >= a.width) {
        if (a.isSigned) {
            return Bits::make(a.value() >= 0 ? 0 : ~static_cast<u128>(0),
                              a.width, true);
        } else {
            return Bits::make(0, a.width, false);
        }
    }

    if (a.isSigned) {
        i128 result = a.value() >> amount;
        return Bits::make(static_cast<u128>(result), a.width, true);
    } else {
        u128 result = a.raw >> amount;
        return Bits::make(result, a.width, false);
    }
}

// casts

// the language's T(e). Narrowing keeps the low bits, widening extends
// per the SOURCE's signedness. Both fall out of make(extended(v), w, s).
Bits castTo(Bits v, unsigned width, bool isSigned) {
    return Bits::make(extended(v), width, isSigned);
}

// comparison

// result is always u1. Operands share signedness (the type checker
// guarantees it). Signed -> compare value() as i128. Unsigned -> compare raw
// as u128; do NOT go through value(), which would read a u128 with its top
// bit set as negative.
Bits compare(Bits a, Bits b, Cmp op) {
    u128 result = 0;

    if (a.isSigned) {
        i128 va = a.value();
        i128 vb = b.value();
        switch (op) {
            case Cmp::Lt: result = (va < vb) ? 1 : 0; break;
            case Cmp::Le: result = (va <= vb) ? 1 : 0; break;
            case Cmp::Gt: result = (va > vb) ? 1 : 0; break;
            case Cmp::Ge: result = (va >= vb) ? 1 : 0; break;
            case Cmp::Eq: result = (va == vb) ? 1 : 0; break;
            case Cmp::Ne: result = (va != vb) ? 1 : 0; break;
        }
    } else {
        u128 ra = a.raw;
        u128 rb = b.raw;
        switch (op) {
            case Cmp::Lt: result = (ra < rb) ? 1 : 0; break;
            case Cmp::Le: result = (ra <= rb) ? 1 : 0; break;
            case Cmp::Gt: result = (ra > rb) ? 1 : 0; break;
            case Cmp::Ge: result = (ra >= rb) ? 1 : 0; break;
            case Cmp::Eq: result = (ra == rb) ? 1 : 0; break;
            case Cmp::Ne: result = (ra != rb) ? 1 : 0; break;
        }
    }

    // return size width 1, unsigned
    return Bits::make(result, 1, false);
}

} // namespace minihls