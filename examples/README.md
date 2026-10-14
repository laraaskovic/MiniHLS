# The examples

Five programs. Together they are the regression suite for the whole project:
the AST interpreter in E3, the MLIR in E4, the optimiser in E5, the
schedules in E7, the generated RTL in E8, the pipelines in E9 and the
area/latency curve in E10 are all measured against these files and no
others. Anything a stage gets wrong shows up here first.

Each `x.hc` has an `x.tests` beside it holding hand-computed input/output
pairs. **Those numbers are an oracle.** They were computed on paper and
checked against a throwaway Python model — never by running this project's
own interpreter, because a suite generated from the thing it tests proves
only that the thing is consistent with itself.

| File | Exercises | First needed by |
|------|-----------|-----------------|
| `max3.hc` | straight-line code, signed comparison, mux, no state at all | E3.S4 interpreter |
| `abs_diff.hc` | the width rules on their own: `i16 - i16` is `i17`, `-i17` is `i18`, and a signedness-changing cast on the way out | E3.S4 interpreter |
| `dot.hc` | loop, two array parameters, loop-carried accumulator, the 33-bit-into-32 cast | E4.S6 `scf.for` with `iter_args` |
| `fir.hc` | a file-scope `const` array (a ROM) and eight multiplies that can share hardware | E8.S5 memories, then E10.S2 |
| `stream_sum.hc` | `in`/`out stream<T>`, `read`/`write` ordering, one value consumed and one produced per iteration | E6.S3 the stream dialect |

Why each one earns its place, beyond the column above:

- **max3** is the one with no loop and no memory, so it is the first thing
  every new stage can be pointed at. It is also the chaining experiment in
  E7.S3 — four operations that fit in one cycle at 6.4 ns and split in two
  when the clock tightens — and it is already hand-written in
  [`hw/max3.mlir`](../hw/max3.mlir) to compare the generated version
  against.
- **abs_diff** exists because subtraction is the rule C gets most wrong and
  the one most likely to be got wrong here too. Its test file is mostly
  boundaries for that reason.
- **dot** is the pipelining example: the accumulator is a distance-1
  loop-carried dependence, which is what sets the minimum II in E9.S1, and
  two array reads per iteration are what make the single-port memory
  constraint bite in E7.S4.
- **fir** is dot with one operand frozen into a ROM, which is what makes it
  the resource-sharing example: the same program built with 1, 2 and 4
  multipliers in E10.S2 gives the latency-versus-area curve. Its taps sum to
  zero so that a constant input has an expected output anyone can check.
- **stream_sum** is the only one with handshakes on its ports, so it is the
  only one that can be run against a producer and a consumer that stall at
  random (E9.S5). Backpressure bugs cannot be found with any of the other
  five.

They are all deliberately tiny — eight elements, sixteen bits. Every
expected value has to be computable by hand, and in E8 a co-simulation of an
eight-element loop finishes in microseconds while a thousand-element one is
a coffee break on every CI run.

Only `dot.hc` carries a pragma, and it is a *request* rather than a promise:
`#pragma pipeline II=1` asks E9 for one iteration per cycle, which `dot`
cannot have — the accumulator is a distance-1 recurrence and two array reads
share one memory port. Reporting requested against achieved, and which of the
two bound it, is E9.S2. Every stage before E9 parses the pragma and ignores
it, which is why no other example carries one: a pragma in the source would
otherwise mean every stage from E4 to E8 had to honour it before the stage
that implements it exists.

## The `.tests` format

Line-based, so that the runner in E3.S5 can read it with `std::getline` and
a few `>>`.

- Blank lines are ignored. `#` starts a comment, to end of line.
- One case per line: `<inputs> -> <outputs>`.
- A value is a decimal integer, optionally signed. An array or a stream is a
  bracketed list: `[1 2 3 4 5 6 7 8]`.
- Inputs appear in parameter order — scalars, arrays, then the contents of
  each `in stream`.
- Outputs are the contents of each `out stream` in parameter order,
  followed by the returned value. For four of the five examples that means
  the right-hand side is a single number; `stream_sum` is the one with a
  list and a number.

```
# i32 dot(i16 x[8], i16 y[8])
# x y -> result
[1 2 3 4 5 6 7 8] [8 7 6 5 4 3 2 1] -> 120
```

That is the whole format. It is not JSON, because JSON would mean pulling in
a parser to read something `>>` already reads.

Expect it to change in E3.S5, when the runner is actually written and the
format has to answer questions these six files do not raise — how to state
an expected compile error, for one. What matters today is that the numbers
exist and are right.
