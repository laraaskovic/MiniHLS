# The MiniHLS input language

A small C-like language for describing hardware. Files use the `.hc` extension.

Every construct here exists because it maps onto a circuit. Every construct
absent from C is absent because it does not — see
[What is deliberately absent](#what-is-deliberately-absent).

- [A complete example](#a-complete-example)
- [Lexical structure](#lexical-structure)
- [Types](#types)
- [Functions](#functions)
- [Statements](#statements)
- [Expressions](#expressions)
- [Width and signedness rules](#width-and-signedness-rules)
- [Runtime behaviour](#runtime-behaviour)
- [Pragmas](#pragmas)
- [What is deliberately absent](#what-is-deliberately-absent)
- [Grammar](#grammar)

---

## A complete example

```c
// A 64-tap dot product.
i32 dot(i16 a[64], i16 b[64]) {
  i32 acc = 0;
  for (u7 i = 0; i < 64; i = i + 1) {
    #pragma pipeline II=1
    acc = acc + a[i] * b[i];
  }
  return acc;
}
```

---

## Lexical structure

**Comments** are `// to end of line` and `/* ... */`. Block comments do not
nest.

**Whitespace** is insignificant except as a token separator.

**Identifiers** match `[A-Za-z_][A-Za-z0-9_]*`, with one exception below.

**Type tokens** are identifiers matching exactly `i[0-9]+` or `u[0-9]+` — for
example `i8`, `u7`, `i32`. These are reserved: you cannot name a variable `i32`.
A name that merely *starts* that way is an ordinary identifier, so `i`, `index`,
and `i32_total` are all fine.

**Keywords:** `if`, `else`, `for`, `return`, `const`, `stream`, `pragma`.

**Integer literals** are decimal (`64`), hexadecimal (`0xFF`), or binary
(`0b1011`). Underscores are permitted as digit separators (`0b1010_1010`). There
are no negative literals; `-5` is unary minus applied to `5`.

**Operators and punctuation:**

```
( ) { } [ ] ; , ? : #
=  == != <  <= >  >=
+  -  *  /  %
&  |  ^  ~  << >>
&& || !
```

---

## Types

### Integers

`iN` is a signed two's complement integer of exactly `N` bits. `uN` is unsigned.
`N` ranges from 1 to 64.

There is no default integer type and no implicit promotion to 32 bits. `u7` is
a seven-bit value and costs seven bits of hardware.

### Arrays

```c
i16 buffer[64];              // a local array, becomes a memory
i16 dot(i16 a[64], ...)      // an array parameter, becomes a memory interface
const i8 coeffs[8] = { ... }; // becomes a ROM
```

Array sizes must be integer literals. Arrays are one-dimensional. A `const`
array must have an initializer and cannot be assigned to.

Arrays are not values: you cannot assign one array to another, pass an array by
value, or return one. Only `array[index]` element access is permitted.

### Streams

```c
void sink(stream<u8> input) { ... }
```

A `stream<T>` parameter becomes a ready/valid port pair. `T` must be an integer
type. Streams are read and written through two builtins:

```c
u8 byte = read(input);   // blocks until valid
write(output, byte);     // blocks until ready
```

"Blocks" means the generated hardware stalls that pipeline stage — there is no
notion of a thread being descheduled.

### `void`

Permitted only as a function return type.

---

## Functions

```c
i32 dot(i16 a[64], i16 b[64]) { ... }
```

A program is one or more functions. One is designated the top-level function on
the command line (`--top dot`) and becomes the generated module.

Parameters may be integers, arrays, `const` arrays, or streams. Integer
parameters become input ports; arrays become memory interfaces; streams become
handshake ports.

There are no function calls other than the stream builtins. Every function is
compiled independently as its own module.

---

## Statements

```c
u8 x;                        // declaration
u8 x = expr;                 // declaration with initializer
i16 buf[32];                 // array declaration
x = expr;                    // assignment
buf[i] = expr;               // element assignment
write(out, expr);            // call statement (void builtins only)
if (cond) stmt else stmt     // conditional
for (init; cond; step) stmt  // loop
return expr;                 // return a value
return;                      // return from a void function
{ stmt* }                    // block, introducing a scope
#pragma ...                  // see Pragmas
```

A `for` loop's initializer is a declaration or an assignment, and its step is an
assignment. The bounds must be resolvable to compile-time constants, since the
loop has to become either unrolled logic or a counter with a fixed limit. That
requirement is checked in semantic analysis, not by the parser.

Semantic analysis finds the trip count by running the loop header at compile
time, with the width rules applied, so wrap-around counts: in
`for (u4 i = 7; i > 0; i = i - 2)`, `1 - 2` becomes 15 as a `u4` and the loop
never ends, which is reported as an error. The body may not assign the
induction variable, and `return` may not appear inside a loop; either would
make the trip count a runtime property.

Declarations are scoped to their enclosing block. Shadowing an outer name is an
error rather than a silent override. Arrays may only be declared at the top
level of the function body, because an array is storage that exists once per
call.

A non-void function must return a value on every path.

---

## Expressions

Precedence, loosest to tightest. All binary operators are left-associative;
the conditional operator is right-associative.

| Level | Operators | Notes |
| ----- | --------- | ----- |
| 1 | `?:` | conditional, right-associative |
| 2 | `\|\|` | logical or, result `u1` |
| 3 | `&&` | logical and, result `u1` |
| 4 | `\|` | bitwise or |
| 5 | `^` | bitwise xor |
| 6 | `&` | bitwise and |
| 7 | `==` `!=` | result `u1` |
| 8 | `<` `<=` `>` `>=` | result `u1` |
| 9 | `<<` `>>` | shift |
| 10 | `+` `-` | additive |
| 11 | `*` `/` `%` | multiplicative |
| 12 | `-` `~` `!` | unary prefix |
| 13 | `a[i]` `f(x)` | postfix |

`/` and `%` by a non-constant divisor are extremely expensive in hardware. They
parse, and semantic analysis will warn.

Unlike C, `&&` and `||` do **not** short-circuit. Both sides are evaluated,
because both sides are circuitry that exists and computes regardless. This
matters only if an operand has a side effect, and the only expressions with side
effects are stream reads — which are therefore rejected inside `&&` and `||`.

---

## Width and signedness rules

This is the core of the language. Arithmetic **grows** so that an operation can
never overflow; narrowing happens only where you explicitly ask for it by
assigning to a narrower type.

The reference implementation of these rules is
[src/support/bits.hpp](src/support/bits.hpp).

### Literals

An integer literal has the smallest unsigned type that represents it.

| Literal | Type |
| ------- | ---- |
| `0` | `u1` |
| `1` | `u1` |
| `5` | `u3` |
| `64` | `u7` |
| `255` | `u8` |

This is deliberate: it stops a stray constant from dragging an expression up to
32 bits. Write `i32 x = 5;` and the assignment extends it.

### Mixed signedness

When a binary operator has one signed and one unsigned operand, the unsigned
operand `uN` is first treated as `i(N+1)` — one extra bit, because the largest
`uN` value does not fit in `iN`. The rules below then apply with both operands
signed. The result is signed.

This is conservative by one bit in some cases. It is never wrong, and semantic
analysis reports the resulting width so nothing is hidden.

### The rules

Let the operands have widths `w1` and `w2` after the mixed-signedness step.

| Operation | Result width | Result signedness |
| --------- | ------------ | ----------------- |
| `a + b`, `a - b` | `max(w1, w2) + 1` | signed if either is |
| `a * b` | `w1 + w2` | signed if either is |
| `a / b`, `a % b` | `w1` | signed if either is |
| `a & b`, `a \| b`, `a ^ b` | `max(w1, w2)` | signed if either is |
| `a << k` (constant `k`) | `w1 + k` | as `a` |
| `a << b` (non-constant) | `w1` | as `a` |
| `a >> b` | `w1` | as `a` |
| `-a` | `w1 + 1` | signed |
| `~a` | `w1` | as `a` |
| `!a`, `a && b`, `a \|\| b` | 1 | unsigned |
| `a == b`, `a < b`, and friends | 1 | unsigned |
| `c ? a : b` | `max(w1, w2)` | signed if either is |

Subtraction takes `max + 1` like addition, and its result is always signed —
`u8 - u8` can be negative.

The conditional operator does *not* add a bit: it selects between existing
values rather than computing a new one. In hardware it is a multiplexer.

A width exceeding 64 bits is an error, reported with the expression that caused
it.

### Assignment

Assignment converts the right-hand side to the declared type of the left:

- **Narrower destination:** truncate, keeping the low bits.
- **Wider destination:** sign-extend if the *source* is signed, zero-extend if
  unsigned.
- **Equal width, different signedness:** reinterpret the bits.

Truncation is silent, since it is the only way to bring a grown value back down.
Semantic analysis reports where it happens so you can see it.

### Worked example

```c
i32 acc = 0;          // 0 is u1, extended to i32
i16 x, y;
acc = acc + x * y;
```

- `x * y` → `i16 * i16` → width `16 + 16 = 32`, signed → `i32`
- `acc + (x*y)` → `i32 + i32` → width `max(32,32) + 1 = 33`, signed → `i33`
- `acc = ...` → truncate `i33` to `i32`

The `i33` intermediate is real: it exists in the datapath. The truncation back to
`i32` is what an accumulator does, and it is visible in the IR rather than
implied.

---

## Runtime behaviour

Every operation has a defined result; nothing is left to the implementation.
This matters because the same program is run by an interpreter, by compiled
MLIR, and as simulated hardware, and all three must agree bit for bit.

| Situation | Result |
| --------- | ------ |
| A scalar declared without an initializer | Zero |
| A local array | Its initializer (zero-filled past the end of the list) at the start of every call |
| `a / 0` | All ones in the result type (`-1` if signed, the maximum if unsigned) |
| `a % 0` | `a` |
| The one overflowing quotient, `min / -1` | Wraps back to `min` |
| Signed `/` and `%` | Truncate toward zero; the remainder takes the dividend's sign |
| A shift amount | Read as unsigned, even from a signed operand |
| `a << k` or `a >> k` with `k` at least the width | Zero, or all sign bits for `>>` on a signed value |
| Operands of any operator | Evaluated left to right; in an assignment `t[i] = v`, `v` before `i` |
| An array index outside the array | An error in the interpreter; unspecified in hardware |
| `read()` with no data left | An error in the interpreter; the hardware waits |

The division-by-zero results are the RISC-V convention: what a divider left to
run produces, and fully defined. Constant indices are checked at compile time;
out-of-bounds accesses with computed indices are the one case the hardware does
not guard against.

---

## Pragmas

Pragmas are hints attached to the construct that encloses them. They are parsed
as statements and bound to their enclosing loop or array during semantic
analysis.

```c
#pragma pipeline II=1        // pipeline the enclosing loop at this initiation interval
#pragma unroll               // fully unroll the enclosing loop
#pragma unroll factor=4      // unroll by a factor
#pragma partition coeffs     // fully partition an array into registers
#pragma partition buf factor=2   // split an array across 2 memories
```

A requested `II` is a *request*. If dependencies or memory ports make it
unachievable, the compiler reports the II it actually achieved and the reason,
rather than silently producing something slower.

An unrecognized pragma is an error, not a warning. A typo in a pragma that
silently did nothing would be worse than a build failure.

---

## What is deliberately absent

| Absent | Why |
| ------ | --- |
| **Pointers** | Hardware memory is separate physical blocks, not one address space. Worse, pointers can alias, and scheduling depends absolutely on knowing which memory operations are independent. |
| **Recursion** | Needs a call stack of runtime-determined depth. A circuit has fixed storage decided at compile time, and no program counter to return to. |
| **Dynamic memory** | `malloc` asks an operating system for pages. There is no operating system and no more pages. |
| **Floating point** | An FPGA floating-point unit is large and slow relative to fixed point. Real DSP designs use fixed point, which the explicit-width integers already express. |
| **`while` loops** | Bounds must be known at compile time to build a counter or unroll. |
| **`break`, `continue`, `goto`** | Would turn a loop's trip count into a runtime property. |
| **Structs** | Not fundamental — just not needed yet. A plausible later addition. |
| **Multi-dimensional arrays** | Same; `a[i*W + j]` works today. |
| **Function calls** | Each function compiles to its own module. Calling would mean either inlining or building a handshake protocol; the stretch goal of stream-connected dataflow is the interesting version of this. |
| **Short-circuit `&&`/`||`** | Both operands are circuitry that computes anyway. See [Expressions](#expressions). |

The absences in the top four rows are structural. The rest are scope decisions.

---

## Grammar

EBNF. `{ x }` is zero or more, `[ x ]` is optional, `|` alternates.

```ebnf
program     = function { function } ;

function    = return-type IDENT "(" [ params ] ")" block ;
return-type = int-type | "void" ;
params      = param { "," param } ;
param       = [ "const" ] int-type IDENT [ "[" INT "]" ]
            | "stream" "<" int-type ">" IDENT ;

int-type    = SIGNED-TYPE | UNSIGNED-TYPE ;   (* i8, u7, ... *)

block       = "{" { stmt } "}" ;

stmt        = block
            | var-decl
            | assign-stmt
            | call-stmt
            | if-stmt
            | for-stmt
            | return-stmt
            | pragma ;

var-decl    = [ "const" ] int-type IDENT [ "[" INT "]" ] [ "=" initializer ] ";" ;
initializer = expr | "{" [ expr { "," expr } [ "," ] ] "}" ;

assign-stmt = lvalue "=" expr ";" ;
lvalue      = IDENT [ "[" expr "]" ] ;

call-stmt   = call ";" ;

if-stmt     = "if" "(" expr ")" stmt [ "else" stmt ] ;
for-stmt    = "for" "(" for-init ";" expr ";" assign ")" stmt ;
for-init    = var-decl-no-semi | assign ;
assign      = lvalue "=" expr ;

return-stmt = "return" [ expr ] ";" ;

pragma      = "#" "pragma" pragma-body ;
pragma-body = "pipeline" "II" "=" INT
            | "unroll" [ "factor" "=" INT ]
            | "partition" IDENT [ "factor" "=" INT ] ;

expr        = conditional ;
conditional = logical-or [ "?" expr ":" conditional ] ;
logical-or  = logical-and { "||" logical-and } ;
logical-and = bit-or      { "&&" bit-or } ;
bit-or      = bit-xor     { "|"  bit-xor } ;
bit-xor     = bit-and     { "^"  bit-and } ;
bit-and     = equality    { "&"  equality } ;
equality    = relational  { ( "==" | "!=" ) relational } ;
relational  = shift       { ( "<" | "<=" | ">" | ">=" ) shift } ;
shift       = additive    { ( "<<" | ">>" ) additive } ;
additive    = multiplicative { ( "+" | "-" ) multiplicative } ;
multiplicative = unary    { ( "*" | "/" | "%" ) unary } ;
unary       = [ "-" | "~" | "!" ] unary | postfix ;
postfix     = primary { "[" expr "]" | "(" [ args ] ")" } ;
primary     = INT | IDENT | "(" expr ")" ;
call        = IDENT "(" [ args ] ")" ;
args        = expr { "," expr } ;
```

The expression grammar above documents precedence. The implementation uses a
Pratt (precedence-climbing) parser, which accepts exactly this language from a
single table of binding powers rather than one function per level.
