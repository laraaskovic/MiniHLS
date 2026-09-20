# Frontend Parser

The frontend turns a `.hc` source file into a structured program tree.

```text
source text
    |
    v
SourceFile       owns text and locations
    |
    v
Lexer            text -> flat tokens
    |
    v
Parser           tokens -> AST
    |
    v
AST printer      AST -> stable source-like text
    |
    v
semantic analysis, interpreter, or MLIR generation
```

The parser does not execute the program and does not decide whether names or
types are semantically valid. It decides whether the token sequence has the
shape described by the language grammar, then stores that shape in the AST.

## The AST

The AST is defined in `ast.hpp`. AST means **abstract syntax tree**. It is a
tree because source expressions and statements are nested.

For example:

```c
a + b * c
```

becomes:

```text
Binary(+)
|-- NameRef(a)
`-- Binary(*)
    |-- NameRef(b)
    `-- NameRef(c)
```

The multiplication is a child of the addition, so later stages do not need to
re-read precedence rules.

Every AST node has a `Range`. The range points back into the original source
file. This allows later diagnostics to identify the exact source location of a
bad expression or statement.

Most child nodes are stored as `std::unique_ptr`. For example, a `Binary` owns
its left and right expressions. When the root `Program` is destroyed, its
whole tree is destroyed automatically.

## Parser State

`Parser` owns a vector of tokens and an index into that vector:

```cpp
std::vector<Token> toks_;
size_t pos_ = 0;
```

The small token helpers are the basic parser vocabulary:

```cpp
peek()       // inspect the next token without consuming it
check(k)     // test the next token kind
accept(k)    // consume k if present; return true or false
advance()    // consume and return the next token
expect(k)    // require k, reporting an error if it is missing
```

A parser function normally follows this pattern:

```text
look at the next token
consume the syntax that belongs to this grammar rule
create the corresponding AST node
return the node
```

`expect` reports an error but does not consume a wrong token. Statement-level
recovery must consume tokens, usually up to `;`, `}`, or end-of-file, so a
single error does not trap the parser in an infinite loop.

## Expression Parsing

Expression parsing is split into layers:

```text
parseConditional
    -> parseExpr
        -> parseUnary
            -> parsePostfix
                -> parsePrimary
```

### `parsePrimary`

This parses the smallest expression units:

```c
42
name
i32(value)
(expression)
```

The mapping is:

```text
IntLiteral  -> IntLit
Identifier  -> NameRef
Type (...)  -> Cast
(...)       -> the inner expression
```

A type token can begin a cast because the lexer makes `i32` and `u8` special
type tokens. A normal function call is not possible in MiniHLS, so there is no
call-versus-cast ambiguity.

### `parsePostfix`

This handles the one allowed indexing form:

```c
x[i]
```

It creates an `Index` node containing the array name and index expression.
The current grammar intentionally does not allow chained indexing such as
`x[i][j]`.

### `parseUnary`

This handles prefix operators:

```c
-x
+x
~x
!x
```

It calls itself recursively, so repeated operators work:

```c
--x
```

becomes:

```text
Unary(-)
`-- Unary(-)
    `-- NameRef(x)
```

### `parseExpr`

This is the precedence-climbing engine for binary operators. Each operator has
a binding power. A larger binding power means tighter binding.

For example:

```text
* / % << >> &  -> 5
+ - | ^        -> 4
comparisons    -> 3
&&             -> 2
||             -> 1
```

For `a + b * c`:

1. Parse `a` as the left side.
2. See `+`, whose binding power is 4.
3. Parse the right side with minimum binding power 5.
4. That right side accepts `b * c`, because `*` has binding power 5.
5. Build `a + (b * c)`.

The recursive call uses `bp + 1`. That makes binary operators left-associative:

```text
a - b - c
```

becomes:

```text
(a - b) - c
```

### `parseConditional`

The ternary operator is handled separately:

```c
a ? b : c
```

The false branch calls `parseConditional()` again, which makes nested ternaries
right-associative:

```c
a ? b : c ? d : e
```

becomes:

```text
a ? b : (c ? d : e)
```

## Statement Parsing

### `parseBlock`

A block consumes `{`, parses statements until `}`, then consumes `}`:

```c
{
  statement;
  statement;
}
```

It creates a `Block` containing a vector of statement nodes.

### `parseStatement`

This is the statement dispatcher. It chooses the specific parser from the
first token:

```text
Type                 -> declaration
Identifier           -> assignment
if                   -> if statement
for or pragma       -> for statement
return               -> return statement
write                -> write statement
{                    -> nested block
```

This is one of the few places where the parser decides which grammar rule to
enter. The individual functions parse the details.

### `parseDeclaration`

Declarations begin with a type and name:

```c
i32 acc = 0;
i16 values[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
```

The parser looks two tokens ahead after `Type Identifier`:

```text
Type Identifier =          -> VarDecl
Type Identifier [          -> ArrayDecl
```

A scalar must have an initializer. `read(stream)` is stored specially in a
`VarDecl` because it is an effectful statement-like operation, not a normal
expression.

### `parseAssign`

Assignments have one of these forms:

```c
x = expression;
a[i] = expression;
x = read(stream);
```

The parser checks for `read` only at the beginning of the complete right-hand
side. This rejects:

```c
x = read(stream) + 1;
```

Expressions remain pure, and stream reads occur only in explicit statement
positions.

### `parseIf`

The parser consumes:

```c
if (condition) {
  ...
} else {
  ...
}
```

The `else` branch may be another `if`, allowing `else if` chains. The AST
stores that branch as either a `Block` or another `If` statement.

### `parseFor`

MiniHLS deliberately restricts loop headers to a hardware-friendly shape:

```c
for (u4 i = 0; i < 8; i = i + 1) {
  ...
}
```

The parser stores the pieces separately in `For`:

```text
induction variable type and name
initializer
comparison operator and limit
step direction and step value
body
optional pragma
```

It parses the header structurally instead of accepting a general expression
and trying to analyze it afterward. Semantic analysis will later check things
such as whether the identifiers all refer to the same variable and whether the
step is valid.

A pragma immediately before a loop is attached to that `For` node:

```c
#pragma pipeline II=1
for (...) { ... }
```

### `parseWrite` and `parseReturn`

These parse the fixed statement forms:

```c
write(output, value);
return value;
```

Both require their punctuation and terminating semicolon.

## Program Parsing

`parseProgram` handles the file-level grammar:

```text
const declarations before the function
exactly one function
const declarations after the function
```

A function contains:

```text
return type
name
parameters
body block
```

Parameters can be scalar, array, input stream, or output stream parameters.

The parser records the syntax. A later semantic pass should decide whether:

- Names have been declared.
- A name is being used before declaration.
- Types match.
- A stream direction is used correctly.
- A loop expression is constant and in range.
- There is exactly one valid return.

## What the Printer Does

The AST printer is **not primarily a tree visualizer**. It is a canonical
source-like printer:

```text
AST -> stable MiniHLS text
```

It is useful for three reasons:

1. It lets you inspect what structure the parser built.
2. It makes precedence visible by printing grouping parentheses.
3. It supports the round-trip test:

```text
source -> parse -> print -> parse -> print
```

The test expects the two printed forms to be identical. If they differ, the
AST or printer lost information.

For example, the printer turns:

```c
a + b * c
```

into:

```c
(a + (b * c))
```

That output is intentionally more parenthesized than normal source. The extra
parentheses make the tree structure explicit and remove formatting ambiguity.

The printer currently prints source-like text, not an indented node diagram.
If you want a visual tree later, that should be a separate debug printer such
as `dumpExprTree()` or `dumpProgramTree()`. Keeping that separate avoids
confusing human-readable source output with debugging output.

## Tests

Parser tests live in `tests/parser_test.cpp`.

The most useful groups are:

```text
precedence tests       verify the expression tree shape
structure tests        verify restricted grammar rules
round-trip tests       verify the AST and printer preserve information
```

Run only parser tests:

```bash
ctest --test-dir build -R '^Parser\\.' --output-on-failure
```

Run everything:

```bash
ctest --test-dir build --output-on-failure
```

The parser does not need MLIR. It can be developed and tested using only the
source file, lexer, parser, AST, printer, and GoogleTest targets.
