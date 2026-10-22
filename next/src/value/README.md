# Module `value`

Run-time values and C arithmetic, shared by constant evaluation in `sema` and by the interpreter.

- `value.hpp`: `Value` (integers up to 128 bits, `float`, `double`, `long double`, byte blobs for records), formats,
  the value's term, encoding to and from object representations.
- `arithmetic.hpp`: integer and floating operations, conversions, bit-fields. Undefined behaviour is reported in the
  result, never trapped on. The value that comes with it is what GCC-compiled run-time code gives; for out-of-range
  conversions of `float` and `double` to `unsigned int` it differs between i386 and x86-64, so `floatToInteger` takes
  the data model.

- `term.hpp`: terms, the record of how a value was computed from the inputs of a run (stage 7).

## Terms

A term records the operations of `arithmetic.hpp` that computed a value from nondeterministic inputs, so that the
validator can ask what the value would be with other inputs (`validate/README.md`, "Search").

- **Recording:** the interpreter calls the `TermTable` function named after the arithmetic.hpp function it applied,
  with the same arguments, and attaches the result to the value (`Value::term()`). Values that depend on no input
  never have a term: the functions check their operands inline, so such an operation costs a few inline checks
  (0.75 ns measured). An operand without a term becomes a constant of its value, with its format. `&&`, `||`, `!`
  and `?:`, which the interpreter evaluates by control flow, have their own kinds; an operand the run did not
  evaluate is missing. `?:` with a condition without term is its selected operand's term.
- **Strict terms:** while the observer evaluates a witness constraint, the interpreter opens a
  `TermTable::StrictScope`. The terms recorded in it are strict (`Term::strict`): like the constraint's evaluation,
  their evaluation stops at any undefined behaviour of their operation, where the program continues after some.
  Their operands recorded outside the scope keep the program's semantics. A scope with `isStrict` false suspends
  strictness for terms that belong to the program, such as the conversion to `_Bool` of an input that a constraint
  reads first.
- **Inputs:** `TermTable::input` registers the run's value of an input with its term, at most once per run and in any
  order. Inputs are numbered in request order (`InputId`).
- **The opaque term** stands for any value that depends on inputs in a way the table does not record: results of
  operations without a term kind (library functions, pointer arithmetic, partial overwrites), to which the interpreter
  attaches `TermTable::opaque()`, and results over a limit. An operation with an opaque operand is opaque too, so no
  term has the opaque term as an operand. It evaluates to no value: the validator cannot predict such a value, but
  never takes it for a constant.
  - Known imprecision: `&&` and `||` with one opaque operand are opaque as a whole, so the validator cannot steer
    `x > 0 && strlen(s) > 3` through `x`, although `x <= 0` alone decides it.
- **Limits** (`TermLimits`): a result whose term would exceed the depth limit, or would not fit into the size limit
  together with its constants, gets the opaque term. Inputs get a term even in a full table, so that a full table does
  not hide later inputs from the search, until the table holds twice the size limit; later inputs get the opaque term,
  so that a loop that reads inputs without end does not exhaust the memory. Conversions of an input (ConvertInteger,
  ToBool, IntegerToFloat, FloatToInteger, ConvertFloat with an Input operand) share that cap, since programs often
  convert an input before they use it (`unsigned char c = x;`). Past the size limit, each conversion of an input (by
  kind, formats and strictness) is recorded once and then found again (`inputConversions_`), so that a loop that
  converts an input again and again takes one term: the conversions are bounded by the inputs times the few
  conversions a program applies to each, and a full table still has room for later inputs. `maxTerms` 0 records
  nothing at all.
- **Memory:** a term takes 32 bytes, and the value of a constant or an input another 64 (`sizeof(Value)`), plus one
  bit per input id. An operation records at most one constant, so at the default size limit of 2^21 terms a table
  takes at most 128 MiB (measured: 128.2 MiB with a constant in every operation). Inputs beyond the size limit take
  another 96 bytes each and conversions of inputs another 32 plus about 48 in `inputConversions_`, up to 2^21 of them
  together: the worst case, a full table followed by inputs up to their cap, measured 353 MiB at its peak.
- **Evaluation** replays the recorded operations with the inputs of a `Substitution` and the run's values for the
  others, through the same arithmetic.hpp functions, so the result is exactly the value the program would compute on
  the same path, and without substitution exactly the value the run computed. Undefined behaviour that the run
  continues after (signed overflow, invalid shifts, out-of-range floating-to-integer conversions) gives the value the
  run continues with. There is no value (`std::expected<Value, NoValue>`) in two cases:
  - `NoValue::EndsRun`: the program would stop at undefined behaviour, one that ends the run (`endsRun`: division by
    zero) or, in a strict term, any.
  - `NoValue::Unknown`: the value depends on the opaque term or on an operand the run did not evaluate.

  The program evaluates every operand of an arithmetic operation, comparison or conversion, so an operand that ends
  the run ends it even if another operand is unknown. Of `&&`, `||` and `?:`, only the operands the program would
  evaluate count, in order: the right operand of `&&` and `||` only if the left one does not decide the result, and
  an unknown left operand makes the result unknown. The validator must reject a candidate under which a kept clause
  ends the run. Whether the program takes the same path with the substituted inputs is not known to the term: every
  prediction is checked by a run. Terms are DAGs with shared subterms: evaluation visits each subterm once, in
  ascending order and without recursion.
- **Terms replace origins**, the notion NITWIT used before stage 7: the input a value was derived from through copies
  and conversions only, which the validator fixed to steer a waypoint.

The host must be x86-64 with SSE (checked at compile time): `float` and `double` arithmetic on the host is then
exactly the target's arithmetic, and the host's `long double` is the x87 extended format of the target.

## Tests

- `tests/unit/value/`: canonical form for every width and signedness; encode/decode round trips including long
  double padding; every operator at the boundaries (0, ±1, minimum, maximum) for 8 to 128 bits, signed and unsigned;
  every undefined-behaviour case and the value produced; shifts by 0, width − 1, width, negative counts; conversions
  between all formats including NaN, infinities, −0.0 and values just outside the integer range; bit-fields across
  byte boundaries, signed and unsigned, width 1 and full width.
- The expected values of the arithmetic tests come from C semantics and were confirmed by compiling the same
  operation with GCC on x86-64 (`-m32` and `-m64`, `-fwrapv` for wrapped results). The tests note where a value is
  hardware behaviour rather than defined C. `data/float_to_integer.txt` holds the results of every floating-to-integer
  conversion for a set of boundary values, generated with gcc-14 `-m64` and `-m32 -msse2 -mfpmath=sse`.
- `term_test.cpp`: every term kind is recorded with inputs as the left, the right or both operands and evaluated with
  the run's values and with substituted ones, against the arithmetic.hpp function applied directly (every operator
  and comparison, wrap-around, shifts with mixed formats, conversions that change the value, floating/integer
  conversions in both data models, NaN and −0.0); undefined behaviour under substitution gives the value the run
  continues with (signed overflow, invalid shifts, an out-of-range conversion whose value differs between the data
  models) or ends the run (division by 0, also in the left operand of `&&` and `||` and in the condition of `?:`);
  which of "ends the run" and "unknown" wins in operations, in `&&` and in `?:`; strict terms (the flag, nested
  scopes, a scope that suspends strictness, any undefined behaviour ending the run, subterms from outside the scope);
  `&&`, `||` and `?:` with missing operands, with a right operand that the left one makes irrelevant, and with
  constant operands; the kind, operator and formats every building function records; constants with their formats,
  also the selected operand of `?:`;
  inputs registered out of order, in a full table and beyond their cap, and conversions of inputs likewise (but not
  conversions of other terms), each recorded once past the size limit, per kind, formats and strictness, and again
  after `clear()`; nothing recorded without an operand with a term; the opaque term (in every table, after
  `clear()`, as the result of operations on it and over the depth and size limits); `maxTerms` 0; shared subterms (a
  term used 60 times, a 64-level chain with each level used twice, a 65535-level chain evaluated without recursion);
  `inputs()`, `subterms()`, `resultFormat()`, `clear()`, and the assertions on preconditions.
