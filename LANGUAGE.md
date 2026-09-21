# The MiniHLS language

MiniHLS source files use the extension `.hc`. A file describes exactly one
hardware module: one function, its interface, and the arithmetic inside it.

The language is C-like on purpose — an FPGA engineer should be able to read
it without a tutorial — but it differs from C in three ways that matter, and
every one of them exists because the target is a circuit and not a CPU:

1. **Every integer carries its width.** There is no `int`. A value is `i16`
   or `u3`; the width is part of the type and part of the hardware.
2. **Arithmetic grows the width instead of overflowing.** `i16 + i16` is
   `i17`, `i16 * i16` is `i32`. Losing bits is never implicit — you write a
   cast, and that cast is where the hardware gets cheaper.
3. **Everything is bounded at compile time.** Loop trip counts, array sizes,
   the whole call graph. A circuit has a fixed number of wires; the language
   refuses to promise anything a fixed amount of hardware cannot deliver.

This document is normative: where it says "is an error", the compiler must
reject the program, and where it defines a behaviour, all three of the
interpreter, the generated MLIR, and the generated RTL must agree on it.

---

## A complete example

```c
// examples/max3.hc
i16 max3(i16 a, i16 b, i16 c) {
  i16 largest = a > b ? a : b;
  return largest > c ? largest : c;
}
```

This compiles to purely combinational hardware: two signed comparators and
two 16-bit multiplexers, all four operations chained into a single clock
cycle. The equivalent hand-written form is already in this repository as
[hw/max3.mlir](hw/max3.mlir) and [hw/max3.sv](hw/max3.sv) — the compiler's
job is to produce that from the four lines above.

A second example, because it exercises everything the first one does not —
arrays, loops, loop-carried state, and an explicit narrowing cast:

```c
// examples/dot.hc
i32 dot(i16 x[8], i16 y[8]) {
  i32 acc = 0;
  for (u4 i = 0; i < 8; i = i + 1) {
    acc = i32(acc + x[i] * y[i]);
  }
  return acc;
}
```

`x[i] * y[i]` is `i32` (16 + 16). Adding it to `acc` is `i33`, one bit wider
than it can be stored in, so the program has to say `i32(...)` — that cast is
the programmer stating, in the source, that the sum is known to fit. The
compiler is then free to build a 32-bit adder rather than a 33-bit one.

---

## Lexical structure

Source is UTF-8; outside comments, only ASCII is permitted.

### Whitespace and comments

Spaces, tabs, carriage returns and newlines separate tokens and are otherwise
insignificant, except that a `#pragma` line is terminated by a newline.

```c
// a line comment, to end of line
/* a block comment,
   which does not nest */
```

An unterminated block comment is an error.

### Identifiers

```
identifier = ( letter | "_" ) , { letter | digit | "_" } ;
```

Identifiers are case-sensitive.

Type names are not a separate lexical class, and the rule is **longest match
first, then classify**: the lexer consumes the maximal run of identifier
characters, and only then looks at the complete lexeme. If the whole lexeme
matches `[iu][1-9][0-9]*` it is a *type* token; otherwise it is a keyword or
an identifier. So `u8` is a type and can never be a variable name, while
`i16x`, `u8_mask`, `iota` and `u08` are ordinary identifiers — classification
never splits a lexeme, so `i16x = 3;` is an assignment and not a bizarre
parse error.

Two consequences of classifying the whole lexeme: a width never has a
leading zero, so there is only one spelling of each type; and a lexeme that
matches the pattern with a width outside 1..64 is a type token whose width is
out of range, reported as exactly that, rather than silently becoming an
identifier.

### Keywords

```
const   else   for   if   in   out   read   return   stream   write
```

All are reserved and cannot be used as identifiers.

### Integer literals

Three bases, with `_` permitted between digits as a readability separator
(never leading, never trailing):

```c
42        1_000_000        // decimal
0xFF      0x1234_5678      // hexadecimal, case-insensitive in digits and prefix
0b1010    0b1111_0000      // binary
```

A literal has no type of its own and no width limit in the source; see
[Untyped constants](#untyped-constants). There are no suffixes (`u`, `L`, and
so on) — an explicit cast such as `u8(42)` covers the same ground with one
mechanism instead of two. There are no character, string or floating-point
literals.

A leading `0` is not an octal prefix: `0755` is seven hundred and fifty-five.

An integer literal may not be immediately followed by an identifier character.
For example, `123abc`, `0b12`, and `0xFG` are invalid integer literals rather
than separate integer and identifier tokens. This catches missing separators
and digits that are not valid in the selected base.

### Operators and punctuation

```
+   -   *   /   %   ~   &   |   ^   <<   >>
!   &&  ||  ==  !=  <   <=  >   >=
=   ?   :   (   )   {   }   [   ]   ,   ;   #
```

---

## Types

Every type in MiniHLS is written out in full; there is no inference and no
default integer type.

### Integers

`iN` is a two's-complement signed integer of exactly `N` bits. `uN` is an
unsigned integer of exactly `N` bits. `N` is a decimal literal with
**1 ≤ N ≤ 64**.

`i16`, `u1`, `u3` and `i64` are all types. There is no `bool`: **`u1` is
the boolean type**, and nothing else is. Comparisons produce `u1`,
`if` and `?:` require `u1`, and `&&`/`||` take and produce `u1`. One type
means one rule: a condition is a one-bit wire, which is exactly what it is in
the hardware.

**There are two width limits, and they differ on purpose.** 64 is the widest
type that can be *written* — in a declaration, a parameter, a return type, a
stream element or a cast. 128 is the widest type an expression may *compute*,
and the widest growth from written operands is exactly the 64 × 64 product.
An operation whose result width would exceed 128 bits is an error, not a
silent truncation; narrow an operand first if you meant it.

The limits have to differ. If the widest writable type were also the widest
computable one, the widest type in the language would be one you could
declare but never add to: `i64 + i64` wants `i65`, and capping results at 64
would make that line an error on its own. The consequence is worth stating
plainly — a value between 65 and 128 bits wide exists only inside an
expression. To keep one, cast it down, which is the same decision the
language asks for everywhere else, recorded in the same place.

### Arrays

```c
i16 buf[8];              // local, zero until written
const i16 taps[4] = { 3, -1, -1, 3 };
```

An array has a fixed element count given by a constant expression ≥ 1, and a
scalar integer element type. Arrays are not values: they cannot be assigned
whole, compared, returned, or nested (`i16 m[4][4]` is not a type). They may
appear as function parameters, as local declarations, and as file-scope
`const` declarations.

Array elements are accessed with `a[i]`, where `i` is unsigned — see
[Indexing](#indexing).

### Streams

```c
in  stream<u8>  s
out stream<u32> r
```

`stream<T>` is a first-in-first-out port carrying values of scalar type `T`.
A stream may appear **only as a function parameter**, and every stream
parameter carries a required direction, `in` or `out`, because that direction
becomes the direction of the `valid`/`ready`/`data` pins on the generated
module. Streams have no length: a program cannot ask how many elements
remain, only read the next one or write one more.

A stream is read with `read(s)` and written with `write(r, v)`; both are
described under [Statements](#statements), not under expressions, because
they have an effect and their ordering is therefore defined.

---

## Functions

```c
i32 dot(i16 x[8], i16 y[8]) { ... }
```

**A file contains exactly one function.** It is the entry point, it becomes
one `hw.module`, and its name becomes the module name. There are no calls:
MiniHLS has no call expression, and therefore no recursion, no call graph and
no interprocedural anything. The reason is scheduling — a call is either an
inlined copy of the callee's hardware or a submodule with its own handshake
and its own schedule, and neither belongs in the first version of a compiler
whose whole job is to schedule one dataflow graph well.

A file may also contain file-scope `const` declarations, before or after the
function. They are compile-time constants; constant arrays become ROMs.

**Parameters** are scalars (`i16 a`), arrays (`i16 x[8]`) or streams
(`in stream<u8> s`). Scalar parameters are latched on `start`. Array
parameters are **read-only** and become memory read ports. A function with no
parameters is legal.

**The return type** is a scalar integer type; there is no `void`. The body
must end with exactly one `return` statement, and `return` may not appear
anywhere else. Early exit is genuinely absent rather than merely unimplemented:
structured control flow lowers to `scf.if` and `scf.for`, which carry values
out as results, and an early `return` from inside a loop is a dynamic exit
those constructs cannot express without predication machinery the rest of the
language does not need.

---

## Statements

### Declarations

```c
i32 acc = 0;              // scalars must be initialised
u8  b   = read(s);        // a stream read, see below
i16 buf[8];               // arrays need not be
i16 win[3] = { 0, 0, 0 }; // but may be, elementwise
```

A scalar declaration requires an initialiser; there are no uninitialised
scalars and therefore no undefined reads. Array elements that are not given
an initialiser read as zero until written — a local array becomes a register
file or a block RAM, and "zero at reset" is both cheap and definable, whereas
"whatever was there" is neither.

The initialiser's type must be [assignable](#assignment-and-implicit-widening)
to the declared type. An array is initialised only by a list and a scalar
only by an expression — the two forms are separate productions in the
[grammar](#grammar), so `const i16 taps[4] = 5;` does not parse. Array
initialiser lists must have exactly as many elements as the array, each a
constant expression.

A declaration introduces a name for the remainder of the enclosing block.
Declaring a name that is already visible in the same block is an error;
shadowing a name from an enclosing block is allowed.

The induction variable declared by a `for` loop is visible in the loop header
and body. A declaration with the same name inside the loop body is rejected;
loop variables cannot be shadowed.

`const` may be applied to a file-scope declaration to make it a compile-time
constant usable in constant expressions. Local `const` is not supported.

### Assignment

```c
acc     = i32(acc + x[i] * y[i]);
buf[k]  = v;
b       = read(s);
```

The left-hand side is a variable or an array element. Assignment is a
statement, not an expression: there is no `a = b = c`, no `+=`, no `++`, and
no assignment inside a condition. Expressions in MiniHLS are pure, which is
why their evaluation order does not have to be specified.

Assigning to a parameter is an error for arrays and streams, and allowed for
scalars (the latched copy is what gets updated).

### `if` / `else`

```c
if (cond) { ... } else if (other) { ... } else { ... }
```

The condition must be `u1`. Braces are **mandatory** on both arms; there is
no dangling-else case to resolve, because there is no brace-less body.

### `for`

The only loop form. Its trip count must be known at compile time:

```c
for (u4 i = 0; i < 8; i = i + 1) { ... }
for (u6 j = 32; j > 0; j = j - 2) { ... }
```

Formally, a loop header is legal when all of the following hold:

| Part | Requirement |
|------|-------------|
| init | declares a fresh induction variable of scalar type, initialised from a constant expression |
| cond | `i OP limit`, with `OP` one of `< <= > >=`, `i` the induction variable, and `limit` a constant expression |
| step | `i = i + step` or `i = i - step`, with `step` a non-zero constant expression |
| direction | the step must move `i` toward the limit (`+` with `<`/`<=`, `-` with `>`/`>=`) |
| range | the induction variable's type must hold every value it takes, **including the final out-of-range value** that ends the loop |

That last row is the one that catches people: `for (u3 i = 0; i < 8; i = i + 1)`
is an error, because the loop must be able to compute `i == 8` to stop, and 8
does not fit in `u3`. Use `u4`.

It catches the countdown direction too, where the bug it prevents is worse
than a lost bit:

```c
for (u6 j = 31; j > 0; j = j - 2) { ... }   // error
```

`j` walks 31, 29, …, 1, and then `1 - 2` is −1, which as a `u6` is 63: the
loop never terminates. The range rule rejects it, because the terminating
value −1 does not fit `u6`. The legal example above, `u6 j = 32`, differs
only in that its terminating value is 0, which does. If the odd start is what
you meant, widen to a signed type — `i7` holds −1.

The induction variable is read-only inside the body; assigning to it is an
error. Its update is exempt from the width-growth rules — `i + 1` would
otherwise be one bit wider than `i` — because the range requirement above has
already proven that every value it takes fits in its declared type. That
proof, not an implicit truncation, is what makes the exemption sound.

The trip count is computed exactly, at compile time, from init, limit and
step. There is no `while`, no `do`, no `break`, no `continue` and no `goto`:
each of them makes the iteration count depend on data, and a scheduler that
cannot bound the iteration count cannot bound the number of cycles or the
amount of hardware. When a loop's bound genuinely is dynamic, the shape that
works in hardware is a `for` over the maximum with a predicated body.

### Stream statements

```c
u8 v = read(s);      // as the whole initialiser of a declaration
v    = read(s);      // or the whole right-hand side of an assignment
write(r, v + 1);     // as a statement of its own
```

`read` may appear **only** in those two positions, never nested inside a
larger expression. That restriction is what keeps expressions pure: with it,
the order in which stream elements are consumed is exactly statement order,
and no rule about operand evaluation order is needed. The value produced by
`read(s)` has the stream's element type. `write(r, v)`'s value must be
assignable to the element type.

Stream operations inside an `if` happen only when that branch is taken.
Stream operations inside a `for` happen once per iteration.

### Blocks

A brace-enclosed sequence of statements is itself a statement, and introduces
a scope.

---

## Expressions

Expressions are pure: no assignment, no calls, no `read`, no side effects of
any kind. Both arms of `?:` and both operands of `&&`/`||` are always
evaluated — **nothing short-circuits** — because the hardware is a
multiplexer whose inputs are all computed anyway. This matters when an arm
can fault, so every operation that could is [defined, not
undefined](#runtime-behaviour).

### Precedence and associativity

Highest binding first. This is the table the parser implements directly.

| Level | Operators | Associativity |
|------:|-----------|---------------|
| 1 | `a[i]` indexing, `T(e)` cast, `(e)` grouping | — |
| 2 | unary `-` `+` `~` `!` | right |
| 3 | `*` `/` `%` `<<` `>>` `&` | left |
| 4 | `+` `-` `\|` `^` | left |
| 5 | `<` `<=` `>` `>=` `==` `!=` | left |
| 6 | `&&` | left |
| 7 | `\|\|` | left |
| 8 | `?:` | right |

**This is Go's table, not C's, and the difference is deliberate.** C binds
`&`, `^` and `|` *looser* than `==`, so `x & mask == 0` means
`x & (mask == 0)` — a documented historical mistake that C compilers now warn
about. In a language where masking is everyday work, that trap is not worth
inheriting. Here all six comparisons sit below all of the arithmetic and
bitwise operators, so `x & mask == 0` parses the way it reads. Shifts bind
like multiplication, so `a + b << 2` is `a + (b << 2)`.

Chained comparison (`a < b < c`) parses, as it does in C, but is almost
always a width error: `a < b` is `u1`, and comparing `u1` with an `i16` mixes
signedness.

### Casts

```c
i32(acc + x[i] * y[i])
u8(x)
i8(u8_value)
```

`T(e)` converts a scalar expression to scalar type `T`. It is the *only* way
to change a value's width or signedness, and it always succeeds — see
[Runtime behaviour](#runtime-behaviour) for exactly what each direction does.

### Indexing

`a[i]` reads (or, as an lvalue, writes) one element of array `a`.

The index must have an **unsigned** type, or be an untyped constant that is
non-negative and less than the array's length. Requiring unsigned indices
removes the negative-index case from the language entirely, which leaves a
single upper-bound check as the only thing the hardware may need to guard.

A constant index that is out of range is a compile-time error. A dynamic
index that is out of range is defined at runtime, below.

Only an array *name* can be indexed, and only once: `a[i][j]` and `u8(x)[0]`
are not productions in the grammar at all, so they fail in the parser rather
than in analysis. There is nothing else they could mean — arrays are not
values, cannot nest, and a cast yields a scalar.

### Untyped constants

An integer literal, and any expression built only from literals, `const`
names, casts and operators, is an **untyped constant**: it is evaluated in
arbitrary precision at compile time and acquires a type from its context.

- Against a typed operand, it takes that operand's type: in `a > 0` with `a`
  of type `i16`, the `0` is `i16`.
- In an initialiser or assignment, it takes the target's type.
- Inside `T(...)`, it takes `T`.
- As the right-hand operand of `<<` or `>>` it is unsigned, and must be
  non-negative; the shift's result type comes from the left operand either
  way.
- Where nothing constrains it, it is an error; write the cast.

The last case is rarer than it looks, because an expression built *only* from
constants is itself an untyped constant: it folds in arbitrary precision and
then takes a type from its context. `if (1 + 2 < 3)` is fine — the comparison
is exact, and the `u1` it produces is what the condition wanted. The genuinely
unconstrained case needs a *non-constant* operand that supplies no type:

```c
// s is a u3 variable, so 1 << s is not a constant expression
if ((1 << s) != 0) { ... }      // error: nothing types the 1
```

A shift takes its width and signedness from its left operand, so `s` types
nothing; the other side of `!=` is an untyped constant too; and there is no
target to fall back on. Write `u8(1) << s`.

If the value does not fit the type it acquires, that is a compile-time error,
and it is reported as such: `u8 x = 300;` and `i16 y = -1;` differ only in
that the second one fits. Note that `-1` is unary minus applied to an untyped
constant, so it is itself an untyped constant of value −1, and it fits any
`iN` but no `uN`.

---

## Width and signedness rules

The distinctive part of the language. Two rules govern everything:

> **Results are wide enough to be exact.** No arithmetic operation can
> overflow, because its result type is chosen to hold every possible value.
>
> **Losing bits is explicit.** The only operations that discard information
> are casts and shifts, and both are written by hand.

### Signedness

**Mixed signedness in a binary operation is an error.** `i16 + u16` does not
compile; write `i17(u16_value)` or `u16(i16_value)` and say which
interpretation you meant. C's usual arithmetic conversions quietly turn a
signed operand unsigned and produce some of the most-reported bugs in the
language; there is no reason to reproduce that in a language where widths are
already written down. The three exceptions, each of which involves an operand
that is not really an arithmetic operand:

- the right-hand operand of `<<` and `>>`, which must be unsigned;
- an array index, which must be unsigned;
- untyped constants, which take the signedness of the context.

### The table

`wa` and `wb` are the operand widths; `S` means both operands signed, `U`
means both unsigned.

| Expression | Operand rule | Result width | Result signedness |
|------------|--------------|--------------|-------------------|
| `a + b` | same signedness | `max(wa,wb) + 1` | same as operands |
| `a - b` | same signedness | `max(wa,wb) + 1` | **always signed** |
| `a * b` | same signedness | `wa + wb` | same as operands |
| `a / b` | S | `wa + 1` | signed |
| `a / b` | U | `wa` | unsigned |
| `a % b` | same signedness | `min(wa,wb)` | same as operands |
| `a & b`, `a \| b`, `a ^ b` | same signedness | `max(wa,wb)` | same as operands |
| `a << b` | `b` unsigned | `wa` | same as `a` |
| `a >> b` | `b` unsigned | `wa` | same as `a` |
| `-a` | any | `wa + 1` | signed |
| `+a` | any | `wa` | same as `a` |
| `~a` | any | `wa` | same as `a` |
| `!a` | `a` is `u1` | 1 | unsigned |
| `a < b`, `<=`, `>`, `>=`, `==`, `!=` | same signedness | 1 | unsigned |
| `a && b`, `a \|\| b` | both `u1` | 1 | unsigned |
| `c ? a : b` | `c` is `u1`; `a`, `b` same signedness | `max(wa,wb)` | same as arms |
| `T(a)` | any | width of `T` | signedness of `T` |

Operands narrower than the result are extended to the operation width first:
sign-extended if signed, zero-extended if unsigned.

Every row is subject to the 128-bit result limit from
[Integers](#integers): where the width the table gives exceeds 128, the
operation is an error, and the fix is a cast on an operand. Because written
types stop at 64, that can only happen to an operand that is itself a wide
intermediate: `u64 * u64` is `u128`, and multiplying *that* by anything at
all is over the limit.

Four rows deserve a sentence each:

- **Subtraction is always signed**, even for unsigned operands, because
  `u8 - u8` can be negative and `u9` cannot represent that. `u8(0) - u8(1)`
  is `i9(-1)`, not `511`. This is the rule that C gets most wrong in
  practice.
- **Signed division grows by one bit** so that the single overflowing case in
  two's complement, `i8(-128) / i8(-1) == 128`, has somewhere to go. That is
  why the result is `i9` and why signed division never overflows here.
- **Remainder takes the narrower width**, because `|a % b| < |b|`.
- **Shifts do not grow.** `a << b` has the width of `a`, and bits shifted off
  the top are discarded. Growing by the largest possible shift would mean a
  `u8` shift amount adds 255 bits to the result — unusable. This is the one
  place where bits are lost without a cast, so it is called out here rather
  than buried: if you want the wide result, widen first, as in
  `i32(x) << 3`.

### Assignment and implicit widening

A value of type `S` may be assigned to, initialised into, returned as, or
passed as type `T` when:

- `S` and `T` have the same signedness, **and**
- `T` is at least as wide as `S`.

Anything else — narrowing, or a change of signedness — requires `T(...)`.
This is the rule that puts the `i32(...)` in the dot product, and it is the
whole point: the compiler cannot know that a 33-bit sum fits in 32 bits, but
the programmer often can, and the cast is where that knowledge is recorded.

---

## Runtime behaviour

Every operation in MiniHLS is **defined for every input**. There is no
undefined behaviour, anywhere, and this is a deliberate stance rather than an
oversight: this project checks an AST interpreter, JIT-executed MLIR, and an
RTL simulation against each other, and three implementations can only agree
where the language has said what the answer is.

MLIR's `arith` dialect leaves several of these cases undefined. Closing that
gap — emitting the guards that make the generated IR match this section — is
MLIRGen's job (roadmap E4.S4), not the optimiser's and not the language's.

| Situation | Defined result |
|-----------|----------------|
| `a / 0` | `0` |
| `a % 0` | `0` |
| `a / b` generally | truncated toward zero |
| `a % b` generally | sign follows the dividend; `(a/b)*b + a%b == a` whenever `b != 0` |
| `a << b`, `b >= wa` | `0` |
| `a >> b`, `b >= wa`, `a` unsigned | `0` |
| `a >> b`, `b >= wa`, `a` signed | `0` if `a >= 0`, else `-1` (all sign bits) |
| `a >> b`, `a` signed | arithmetic shift (sign-extending) |
| `a >> b`, `a` unsigned | logical shift (zero-filling) |
| narrowing cast `T(e)` | the low `width(T)` bits of `e`, reinterpreted per `T`'s signedness |
| widening cast `T(e)` | sign-extended if `e` is signed, zero-extended if unsigned |
| same-width cast `T(e)` | the same bits, reinterpreted per `T`'s signedness |
| read of `a[i]`, `i >= len(a)` | `0` |
| write to `a[i]`, `i >= len(a)` | no effect |
| read of a never-written element | `0` |

The out-of-range array cases are defined rather than forbidden because the
index can be dynamic. They cost a comparator and a mux — but only when the
compiler cannot prove the index is in range, and inside a counted `for` it
almost always can, so the guard usually optimises away. Any program where it
does not is a program that was about to read out of bounds.

Division by zero yields `0` rather than, say, all-ones because `0` is one
constant for both `/` and `%` and both signednesses, which is one mux input
instead of four.

---

## Pragmas

A pragma occupies a line of its own and attaches to the `for` statement that
immediately follows it. Attaching one to anything else is an error. An
unrecognised pragma is an error, not a warning — a silently ignored
optimisation directive is worse than none.

```c
#pragma unroll
for (u4 i = 0; i < 8; i = i + 1) { ... }        // fully unrolled

#pragma unroll factor=2
for (u6 i = 0; i < 32; i = i + 1) { ... }       // two copies per iteration

#pragma pipeline II=1
for (u6 i = 0; i < 32; i = i + 1) { ... }       // start an iteration each cycle
```

| Pragma | Meaning |
|--------|---------|
| `#pragma unroll` | Replace the loop by `trip_count` copies of its body. |
| `#pragma unroll factor=N` | Replace the loop by one whose body is `N` copies, `N ≥ 2`. If `N` does not divide the trip count, the remainder iterations are emitted separately. |
| `#pragma pipeline II=N` | Schedule the loop so a new iteration starts every `N` cycles, `N ≥ 1`. |

Both are **requests, not guarantees**. A requested initiation interval that
the loop's recurrences or its memory ports make impossible is reported, along
with the II actually achieved and the reason for the difference (roadmap
E9.S2) — a tool that silently ignores `II=1` teaches you nothing about why
your loop is slow. `unroll` and `pipeline` on the same loop is an error:
unrolling removes the loop that pipelining would schedule.

---

## What is deliberately absent, and why

Each of these is missing for a hardware reason, not because it was hard.

| Absent | Why |
|--------|-----|
| Pointers and references | A pointer is an address into one flat memory. Hardware has many small disjoint memories and no address space; an aliasing pointer would make it impossible to know which memory an access touches, and therefore impossible to schedule its port. |
| `malloc` / dynamic allocation | Memory is allocated at synthesis time, in gates. There is no allocator to call and no way to grow. |
| Recursion and function calls | Recursion needs an unbounded stack; hardware has a fixed one or none. Calls in general need either inlining or a submodule with its own handshake and schedule — see [Functions](#functions). |
| `while`, `do`, `break`, `continue`, `goto` | Each makes the iteration count data-dependent, so neither the cycle count nor the amount of hardware can be bounded at compile time. A predicated `for` over the worst case is the shape that synthesises. |
| Floating point | An IEEE-754 unit is thousands of gates and multiple cycles per operation. Fixed-point arithmetic on `iN`/`uN` is what fits in an FPGA, and choosing the widths is the engineering. |
| Structs, unions, enums | Composite layout adds frontend work and no new hardware concept; a struct is just several values, which the language already has. |
| Multidimensional arrays | Same reason, plus the address arithmetic would need width rules of its own. `a[i*W + j]` works today. |
| Strings and characters | No character type, no I/O, nothing to print to. |
| `int`, `long`, integer promotion | The width *is* the hardware. A default width would silently pick a circuit size. |
| Implicit narrowing | Dropping bits is a design decision about how large a circuit needs to be. It should appear in the source. |
| Early `return`, assignment expressions, `++`, `+=` | Each is sugar whose absence costs one line of source and removes a whole class of ordering and control-flow questions from the middle of the compiler. |
| Global mutable state | A mutable global is a register with no defined owner and no defined access schedule. File-scope `const` covers the real use, which is ROM tables. |

Most of these are absences the language may outgrow — structs and calls
first, most likely. `while` and floating point are not on that list.

---

## Grammar

EBNF. `{ x }` is zero or more, `[ x ]` is optional, `|` is alternation,
quoted strings are terminals. Whitespace and comments are removed by the
lexer and are not shown.

```ebnf
(* ---- programs ---------------------------------------------------- *)

program        = { const_decl } , function , { const_decl } ;

const_decl     = "const" , scalar_type , identifier ,
                 "=" , const_expr , ";"
               | "const" , scalar_type , identifier ,
                 "[" , const_expr , "]" , "=" , array_init , ";" ;

function       = scalar_type , identifier ,
                 "(" , [ param , { "," , param } ] , ")" , block ;

param          = scalar_type , identifier
               | scalar_type , identifier , "[" , const_expr , "]"
               | direction , "stream" , "<" , scalar_type , ">" , identifier ;

direction      = "in" | "out" ;

(* ---- types ------------------------------------------------------- *)

scalar_type    = ( "i" | "u" ) , width ;            (* one token; 1 <= width <= 64 *)
width          = nonzero_digit , { digit } ;        (* no leading zero *)

(* ---- statements -------------------------------------------------- *)

block          = "{" , { statement } , "}" ;

statement      = var_decl
               | array_decl
               | assign_stmt
               | write_stmt
               | if_stmt
               | for_stmt
               | return_stmt
               | block ;

var_decl       = scalar_type , identifier , "=" , rhs , ";" ;

array_decl     = scalar_type , identifier , "[" , const_expr , "]" ,
                 [ "=" , array_init ] , ";" ;

assign_stmt    = lvalue , "=" , rhs , ";" ;

lvalue         = identifier
               | identifier , "[" , expression , "]" ;

rhs            = expression | read_expr ;

read_expr      = "read" , "(" , identifier , ")" ;

write_stmt     = "write" , "(" , identifier , "," , expression , ")" , ";" ;

array_init     = "{" , expression , { "," , expression } , [ "," ] , "}" ;

if_stmt        = "if" , "(" , expression , ")" , block ,
                 [ "else" , ( block | if_stmt ) ] ;

for_stmt       = [ pragma ] ,
                 "for" , "(" , for_init , ";" , for_cond , ";" , for_step , ")" ,
                 block ;

for_init       = scalar_type , identifier , "=" , const_expr ;
for_cond       = identifier , rel_op , const_expr ;
for_step       = identifier , "=" , identifier , ( "+" | "-" ) , const_expr ;
rel_op         = "<" | "<=" | ">" | ">=" ;

return_stmt    = "return" , expression , ";" ;

(* ---- pragmas ----------------------------------------------------- *)

pragma         = "#" , "pragma" , pragma_body , newline ;
pragma_body    = "unroll" , [ "factor" , "=" , integer_literal ]
               | "pipeline" , "II" , "=" , integer_literal ;

(* ---- expressions ------------------------------------------------- *)
(* One rule per precedence level; see the table above.                *)

expression     = conditional ;

conditional    = logical_or , [ "?" , expression , ":" , conditional ] ;
logical_or     = logical_and , { "||" , logical_and } ;
logical_and    = comparison  , { "&&" , comparison } ;
comparison     = additive    , { ( "<" | "<=" | ">" | ">=" | "==" | "!=" ) ,
                                 additive } ;
additive       = multiplicative , { ( "+" | "-" | "|" | "^" ) ,
                                    multiplicative } ;
multiplicative = unary , { ( "*" | "/" | "%" | "<<" | ">>" | "&" ) , unary } ;
unary          = ( "-" | "+" | "~" | "!" ) , unary
               | postfix ;
postfix        = identifier , "[" , expression , "]"
               | primary ;
primary        = integer_literal
               | identifier
               | cast
               | "(" , expression , ")" ;
cast           = scalar_type , "(" , expression , ")" ;

const_expr     = expression ;   (* must evaluate at compile time *)

(* ---- lexical ----------------------------------------------------- *)

identifier     = ( letter | "_" ) , { letter | digit | "_" } ;

integer_literal = dec_literal | hex_literal | bin_literal ;
dec_literal    = digit , { digit | "_" } ;
hex_literal    = "0" , ( "x" | "X" ) , hex_digit , { hex_digit | "_" } ;
bin_literal    = "0" , ( "b" | "B" ) , bin_digit , { bin_digit | "_" } ;

letter         = "A".."Z" | "a".."z" ;
digit          = "0".."9" ;
nonzero_digit  = "1".."9" ;
hex_digit      = digit | "A".."F" | "a".."f" ;
bin_digit      = "0" | "1" ;
```

Two notes on the grammar as written:

- It is ambiguous between `cast` and `postfix` only in appearance: a
  `scalar_type` is a single token, produced by the whole-lexeme rule under
  [Identifiers](#identifiers), and can never begin an identifier. So `u8(x)`
  and `f(x)` are distinguished by one token of lookahead — and `f(x)` is not
  a production at all, because there are no calls.
- `const_expr` is syntactically an ordinary expression. Its restriction —
  that it must fold to a value at compile time — is a semantic rule, checked
  in analysis (roadmap E3.S3), not something the parser enforces.
