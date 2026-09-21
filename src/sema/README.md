# Semantic Analysis

The parser answers:

> Does this token sequence have the right syntactic shape?

Semantic analysis answers:

> What does each name and expression mean, and is the program legal?

The semantic-analysis stage works on the parser's AST. It does not rebuild the
AST. Instead, each story progressively annotates the same tree:

```text
parsed AST
   |
   +-- S1: names resolve to Symbols
   +-- S2: expressions receive Types
   +-- S3: constants and loop bounds are evaluated
   +-- S4: the program can execute
   `-- S5: the command-line test harness runs programs
```

The sema code is intentionally separate from the lexer and parser. The lexer
knows what text looks like, the parser knows what syntax looks like, and sema
knows what declarations and values mean.

## E3 Stories

| Story | What it produces | Rough size |
|---|---|---:|
| **S1** | Name resolution: every `NameRef` knows its declaration | ~200 lines |
| **S2** | Type checking: every expression knows its `Type` | ~300 lines |
| **S3** | Constant evaluation and trip counts: constants are folded and loop iteration counts are known | ~200 lines |
| **S4** | Interpreter: programs actually execute | ~400 lines |
| **S5** | `minihls run` and the test harness | ~100 lines |

This file will grow as each story is implemented.

## S1: Name Resolution

S1 is implemented in:

```text
symbol.hpp       Symbol and SymbolKind
scope.hpp        lexical scope stack
resolver.hpp     Resolver interface
resolver.cpp     AST traversal and name checks
```

The result of S1 is that a use of a name is connected to the declaration it
refers to.

For example:

```c
i16 f(i16 x) {
  i16 y = x;
  return y;
}
```

The parser initially creates strings:

```text
NameRef("x")
NameRef("y")
```

The resolver attaches pointers:

```text
NameRef("x") -> Symbol for parameter x
NameRef("y") -> Symbol for local y
```

Later stages can follow the pointer instead of searching for a string again.

### Symbols

A `Symbol` represents one declaration:

```cpp
struct Symbol {
  SymbolKind kind;
  std::string name;
  Type type;
  Range declRange;
  bool isStreamOut;
  uint64_t arrayLength;
};
```

`SymbolKind` records what sort of declaration it is:

```text
ScalarParam       scalar function parameter
ArrayParam        array function parameter
StreamParam       input or output stream parameter
Local             local scalar
LocalArray        local array
GlobalConst       file-scope scalar constant
GlobalConstArray  file-scope array constant
InductionVar      for-loop variable
```

The kind matters because identical-looking syntax can have different rules.
For example, assigning to a local array element is allowed, while assigning to
an array parameter is rejected because array parameters are read-only.

Symbols live in a `std::deque`. A deque gives each symbol a stable address as
more symbols are added. AST nodes store `Symbol*`, so stable addresses are
required.

### Scopes

`ScopeStack` stores nested maps from names to symbols.

```text
lookup:   search from innermost scope outward
 declare: reject only duplicates in the current scope
```

This gives the language its lexical-scoping behavior:

```c
i16 f() {
  i16 x = 1;
  {
    i16 x = 2; // legal shadowing
  }
  i16 x = 3;   // illegal redeclaration in the same scope
  return x;
}
```

The inner `x` temporarily hides the outer `x`. A second declaration in the
same scope is an error.

### Two-pass global constants

The grammar allows file-scope constants before or after the function. S1
defines all global constant symbols before resolving any initializer or
function body:

```text
pass 1: declare every global constant
pass 2: resolve constant initializers and the function
```

That is why this is legal:

```c
i16 f(i16 x[8]) {
  return taps[0];
}

const i16 taps[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
```

A one-pass resolver would incorrectly report `taps` as unknown.

### What S1 checks

S1 reports errors for:

- Unknown names
- Redeclarations in the same scope
- Assignment to constants
- Assignment to array parameters
- Assignment to induction variables
- Assignment to an array without an index
- Indexing a non-array
- Reading from an output stream
- Writing to an input stream
- Reading or writing a non-stream

Each error uses the AST node's `Range`, so diagnostics can point to the use
site. Redeclaration errors also report the original declaration's range.

S1 deliberately does **not** perform type checking or constant evaluation.
Those require later semantic information and belong to S2 and S3.

## S2: Type Checking

S2 will add type annotations to expressions. The intended AST shape is:

```cpp
struct Expr {
  Type type;
  ...
};
```

The type checker will walk the same AST and enforce rules such as:

```text
same signedness for ordinary binary operations
addition grows by one bit
multiplication sums operand widths
comparison produces u1
casts explicitly change width or signedness
shift amounts are unsigned
conditions are u1
```

S2 should use the language's operator table as the single source of truth.
The resolver answers which declaration a name refers to; the type checker then
reads that symbol's declared type.

Example:

```c
i16 x = 1;
i17 y = x + x;
```

The `NameRef` nodes resolve to `x`, and the binary expression receives type
`i17`.

S2 should report errors such as:

- Unknown or incompatible operand types
- Mixed signedness where it is forbidden
- Assignments that narrow implicitly
- Non-`u1` conditions
- Invalid stream element types
- Results wider than the 128-bit expression limit

## S3: Constant Evaluation and Trip Counts

S3 evaluates expressions that must be known at compile time:

```c
const i16 N = 8;

for (u4 i = 0; i < N; i = i + 1) {
  ...
}
```

This is not parser work. The parser only records the expression tree. S1
resolves `N` to its `GlobalConst` symbol. S3 then evaluates the initializer and
stores the folded value on the constant declaration or its symbol.

S3 will also calculate loop information, such as:

```text
initial value
limit
comparison direction
step
trip count
```

The purpose is hardware construction: the compiler must know that loops have
bounded, compile-time behavior.

S3 should reject things such as:

- A constant initializer that depends on a runtime value
- A loop bound that cannot be folded
- A zero loop step
- A loop whose trip count cannot be determined
- Values that do not fit the required type or range

## S4: Interpreter

The interpreter is the golden reference for every later compiler stage. It
will execute the typed, resolved AST using the fixed-width `Bits` runtime.

The interpreter must implement:

- Variable and array storage
- Function parameters
- Blocks and lexical scopes
- `if` and `else`
- Bounded `for` loops
- Stream reads and writes
- All expression operators
- Defined division, remainder, and shift behavior
- Explicit casts and width growth

The important correctness rule is that the interpreter must follow the
language specification, not ordinary C++ behavior. Generated MLIR and RTL will
later be compared against its results.

The six `.hc` examples and matching `.tests` files become the main behavioral
regression suite:

```text
abs_diff
max3
popcount
dot
fir
stream_sum
```

## S5: `minihls run` and the Harness

S5 will expose the interpreter through the command line:

```bash
minihls run examples/abs_diff.hc examples/abs_diff.tests
```

The harness will:

1. Load and lex the source file.
2. Parse it into an AST.
3. Resolve names.
4. Type-check the program.
5. Evaluate constants and loop bounds.
6. Run the interpreter for each test case.
7. Compare actual outputs with expected outputs.
8. Print useful diagnostics and return a nonzero exit code on failure.

S5 is the point where the frontend becomes something users can run rather
than only a library tested through C++ unit tests.

## Current Boundary

At the end of S1:

```text
source -> lexer -> parser -> AST -> resolver -> annotated AST
```

The repository has not yet implemented:

```text
annotated AST -> typed AST       (S2)
typed AST -> folded AST          (S3)
folded AST -> runtime behavior   (S4)
command line test execution      (S5)
```

That separation is intentional. Each stage adds one kind of knowledge and can
be tested independently before the next stage depends on it.
