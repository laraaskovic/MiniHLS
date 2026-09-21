# MLIR Generation

Semantic analysis answers:

> What does each name and expression mean, and is the program legal?

MLIR generation answers:

> What operations does this program correspond to?

This is the stage where the compiler stops describing the source program and
starts describing a circuit. It reads the annotated AST that the front end and
sema produced, and it writes MLIR. Nothing flows back the other way: the AST is
never modified here, and no MLIR is ever read back into an AST.

```text
source -> lexer -> parser -> resolver -> type checker -> const eval
                                                             |
                                                             v
                                                    annotated AST
                                                             |
                                                             v
                                                     MLIRGen (this stage)
                                                             |
                                                             v
                                                        MLIR module
```

Run it:

```bash
./build-mlir/src/minihls emit-mlir examples/max3.hc
```

---

## The two worlds

Almost every confusion in this directory comes from forgetting which side of
the fence a word lives on. Both sides have a thing called a "type", both have
a thing called a "block", and they mean different things.

| Concept | The AST side (ours, `frontend/ast.hpp`) | The MLIR side (theirs) |
|---|---|---|
| a whole program | `Program` | `mlir::ModuleOp` |
| a function | `Function` | `func.func` |
| a statement list | `Block` (a `Stmt`) | `mlir::Block` (holds operations) |
| an expression | `Expr` subclasses | nothing — expressions become operations |
| a computed thing | an `Expr` node | `mlir::Value` (an operation's result) |
| a declaration | `Symbol*` | nothing — see below |
| a type | `Type{width, isSigned}` | `mlir::Type` — `i16`, **signless** |
| a source position | `Range` (byte offsets) | `mlir::Location` |
| comparison | `Binary(Tok::Gt)` | `arith.cmpi sgt` |
| `?:` | `Ternary` | `arith.select` |
| `return` | `Return` | `func.return` |

Two rows deserve emphasis.

**A `Symbol*` has no MLIR counterpart.** There is no such thing as a variable
in MLIR. A variable in the AST becomes an `mlir::Value` — and after that the
name is gone. See *The two rules* below.

**`mlir::Type` has no signedness.** `i16` in MLIR is sixteen bits and nothing
else, exactly like a wire in Verilog. Our `Type` carries `isSigned`, and
`typeOf()` throws it away. It is not lost, though: it reappears in the *choice
of operation*. `a > b` on signed operands is `arith.cmpi sgt`; on unsigned
operands it is `arith.cmpi ugt`. Same two wires, different comparator.

---

## The four tools

The whole file is built out of these.

**`builder_`** — an `mlir::OpBuilder`. It has a cursor. `SomeOp::create(builder_, loc, args...)`
appends an operation at the cursor and hands back its result as an
`mlir::Value`. Setting the cursor is how you say "put the next operations
inside this function".

**`loc(Range)`** — turns our byte offset into an `mlir::Location` carrying
file, line and column. Every operation gets one, so a later MLIR pass can
blame the right line of `.hc` source.

**`typeOf(Type)`** — our `{width = 16, isSigned = true}` becomes MLIR's `i16`.
Signedness is dropped here. It asserts on a *poly* type, which is an untyped
literal that sema failed to pin down; poly has the default width of 1, so
emitting it would quietly produce an `i1` of the right value and the wrong
width.

**`values_`** — a `DenseMap<const Symbol*, mlir::Value>`. It answers one
question: *which SSA value currently holds this variable?* This map is the
heart of the stage.

---

## Step by step: `max3`

The source:

```c
i16 max3(i16 a, i16 b, i16 c) {
  i16 largest = a > b ? a : b;
  return largest > c ? largest : c;
}
```

### Step 1 — make a module

Create an `mlir::ModuleOp` and point the builder's cursor inside it. In MLIR
an operation is owned by its parent block, and a top-level module has no
parent, so the module is held in an `OwningOpRef` from the moment it exists —
that reference is the only thing that will ever free it.

### Step 2 — build the function signature

Walk `fn.params` on the AST side, call `typeOf` on each, and get the MLIR
function type `(i16, i16, i16) -> i16`. Create the `func.func`, then call
`addEntryBlock()`. MLIR creates a block whose *block arguments* are the
parameters: `%arg0`, `%arg1`, `%arg2`.

### Step 3 — bind names to values

```cpp
values_[fn.params[i].symbol] = entry->getArgument(i);
```

```text
Symbol for a  ->  %arg0
Symbol for b  ->  %arg1
Symbol for c  ->  %arg2
```

**This is the moment a name becomes a value.** After this line, nothing
downstream ever looks up the string `"a"` again. It looks up a `Symbol*` and
receives an `mlir::Value`.

### Step 4 — statement one

`i16 largest = a > b ? a : b;` is a `VarDecl` whose initialiser is a
`Ternary`. Emitting an expression is a recursive walk that returns the
`mlir::Value` holding its result:

```text
emit(Ternary)
|
+-- emit(cond) = Binary(Gt, NameRef a, NameRef b)
|   +-- emit(NameRef a) -> values_.lookup(a) -> %arg0     <- emits NOTHING
|   +-- emit(NameRef b) -> values_.lookup(b) -> %arg1     <- emits NOTHING
|   `-- create arith.cmpi sgt, %arg0, %arg1          ->  %0
|
+-- emit(then) = NameRef a -> %arg0                       <- emits NOTHING
+-- emit(else) = NameRef b -> %arg1                       <- emits NOTHING
`-- create arith.select %0, %arg0, %arg1             ->  %1

then, for the declaration itself:
    values_[Symbol for largest] = %1                      <- emits NOTHING
```

The predicate `sgt` rather than `ugt` comes from `n.lhs->type.isSigned` on the
AST side. That is the only place the discarded signedness is used.

### Step 5 — statement two

`return largest > c ? largest : c;`

```text
emit(Ternary)
+-- Binary(Gt, NameRef largest, NameRef c)
|   +-- lookup(largest) -> %1        <- the value from step 4
|   +-- lookup(c)       -> %arg2
|   `-- arith.cmpi sgt           ->  %2
+-- lookup(largest) -> %1
+-- lookup(c)       -> %arg2
`-- arith.select                 ->  %3

create func.return %3
```

### The result

```mlir
module {
  func.func @max3(%arg0: i16, %arg1: i16, %arg2: i16) -> i16 {
    %0 = arith.cmpi sgt, %arg0, %arg1 : i16
    %1 = arith.select %0, %arg0, %arg1 : i16
    %2 = arith.cmpi sgt, %1, %arg2 : i16
    %3 = arith.select %2, %1, %arg2 : i16
    return %3 : i16
  }
}
```

Four source lines, four operations: two comparators and two multiplexers.

---

## The two rules

Notice what is **not** in that output: `largest`. No allocation, no store, no
load. It became `%1` and the name evaporated.

That is SSA construction, and for straight-line code it is two rules:

```text
read a variable    ->  look it up in values_
assign a variable  ->  REPLACE its entry in values_
```

Three lines of code. Assignment does not write to storage; it rebinds a name:

```c
i16 m = a;   // values_[m] = %arg0
m = b;       // values_[m] = %arg1     <- the old entry is simply overwritten
return m;    // returns %arg1
```

That function emits exactly one operation — the `return`.

Shadowing needs no special handling, because the map is keyed on `Symbol*` and
the resolver already gave an inner `m` a different `Symbol` from an outer `m`.
Spelling never enters into it.

**These two rules stop working the moment control flow merges.** If an `if`
assigns `m` on one path and not the other, "the value currently held by `m`"
has two answers, and a map cannot store both. That is what block arguments are
for, and it is why `if` is deferred to S5 (`scf.if` results) and `for` to S6
(`scf.for` with `iter_args`).

---

## Three things that emit nothing

Worth stating plainly, because each one surprises people.

**A `NameRef`.** Reading a variable is a map lookup. The value already exists;
re-reading it does not compute anything.

**A `VarDecl`.** Declaring `largest` does not create storage. It adds one entry
to `values_`.

**An `Assign`.** It replaces one entry in `values_`.

The only AST nodes that produce operations are the ones that actually compute
something: `IntLit`, `Binary`, `Ternary`, and `Return`.

---

## What S2 refuses, and why

`arith` requires **both operands of a binary operation to have the same type.**
Our width rules say `i16 + i16` is `i17` and `i16 * i16` is `i32`, so the
operands of every arithmetic operator need extending before the operation can
be built. Emitting `arith.addi` on an `i16` and an `i16` to produce an `i17`
is IR the verifier rejects.

So S2 emits comparisons — where both operands already share a type and the
result is `i1` — and refuses the rest with a diagnostic naming the story that
will handle it:

| Construct | Refused until |
|---|---|
| `+ - * / % & \| ^ << >>` | S3 (`extsi` / `extui` / `trunci`) |
| casts, unary operators | S3 |
| `if` | S5 (`scf.if`) |
| `for` | S6 (`scf.for`) |
| arrays, indexing | S7 (`memref`) |
| streams | E6 (a custom dialect) |

`max3` was chosen as this story's target precisely because it needs none of
them: `a > b` compares two `i16`s, and both `?:` arms are already `i16`.

### The failure discipline

Every refusal goes through one function:

```cpp
mlir::Value fail(Range where, const std::string& message) {
  diags_.error(where, message);
  failed_ = true;
  return {};
}
```

Every caller checks the null `Value`, and `emit()` returns no module at all
when `failed_` is set. The rule is: **either a complete module, or a
diagnostic and nothing.** Half a module that trips the verifier gives you an
error message about the symptom, thirty operations away from the cause.

This is also why `mlir::verify()` in `emitMlirFile` is a real check rather
than a formality.

### File-scope constants

A `const` at file scope has no SSA value until something reads it. `constants_`
notes where the declarations are, and each *read* emits a fresh
`arith.constant` at that point.

Fresh at every read, deliberately not cached: from S5 onward the builder's
cursor can be inside an `scf.if` or `scf.for` region, and a constant created
in that region would not dominate a use after the region ends. Duplicates are
exactly what the `cse` pass removes, so this costs nothing once E5 exists.

---

## Files

```text
mlirgen.hpp    emitModule() and emitMlirFile()
mlirgen.cpp    the MLIRGen class: one emit() for expressions,
               one emit() for statements, and the four tools
```

`emitModule` takes an already-checked `Compilation` and returns a module, or
null with the reason in `c.diags`. `emitMlirFile` is the `emit-mlir`
subcommand around it: compile, emit, verify, print.

This code lives in the `minihls_mlir` library, not `minihls_core`, so a build
configured with `-DMINIHLS_ENABLE_MLIR=OFF` still compiles the whole front end
and interpreter. That is the configuration CI uses.

Tests are in `tests/mlirgen_test.cpp`, which compiles source strings and
compares the printed IR text.

---

## E4 Stories

| Story | What it produces | State |
|---|---|---|
| **S1** | Read MLIR by hand — `docs/mlir-by-hand/` | done |
| **S2** | Module, `func.func`, scalar params, constants, comparisons, `?:`, `return` | done |
| **S3** | Width rules as explicit `extsi` / `extui` / `trunci` casts | next |
| **S4** | Guards for defined behaviour — division by zero and over-wide shifts are undefined in `arith` but defined in our language | |
| **S5** | `if` / `else` as `scf.if`, with results | |
| **S6** | `for` as `scf.for` with `iter_args` | |
| **S7** | Arrays as `memref`, constant arrays as globals | |
| **S8** | Differential test: JIT the generated MLIR and compare against the AST interpreter | |

S3 is the one this story was shaped to set up. Every operator that currently
refuses becomes an extension followed by the operation.
