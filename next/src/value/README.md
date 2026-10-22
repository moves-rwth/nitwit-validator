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
  with the same arguments, whenever an operand has a term, and attaches the result to the value (`Value::term()`).
  Values that depend on no input never have a term and cost one check per operation. An operand without a term
  becomes a constant of its value. `&&`, `||`, `!` and `?:`, which the interpreter evaluates by control flow, have
  their own kinds; an operand the run did not evaluate is missing.
- **Limits** (`TermLimits`): a result whose term would exceed the depth limit, or any result after the table reached
  its size limit, gets no term. A missing term costs only precision: the validator then cannot predict the value.
- **Evaluation** replays the recorded operations with the inputs of a `Substitution` and the run's values for the
  others, through the same arithmetic.hpp functions, so the result is exactly the value the program would compute on
  the same path. Undefined behaviour, and an operand the run did not evaluate, give no value. Whether the program
  takes the same path with the substituted inputs is not known to the term: every prediction is checked by a run.
- **Origins:** a term that is an input through conversions only is that input's *origin* (`TermTable::origin`), the
  notion NITWIT used before terms existed.

The host must be x86-64 with SSE (checked at compile time): `float` and `double` arithmetic on the host is then
exactly the target's arithmetic, and the host's `long double` is the x87 extended format of the target.

## Tests

- `tests/unit/value/`: canonical form for every width and signedness; encode/decode round trips including long
  double padding; every operator at the boundaries (0, ±1, minimum, maximum) for 8 to 128 bits, signed and unsigned;
  every undefined-behaviour case and the value produced; shifts by 0, width − 1, width, negative counts; conversions
  between all formats including NaN, infinities, −0.0 and values just outside the integer range; bit-fields across
  byte boundaries, signed and unsigned, width 1 and full width.
- The expected values come from C semantics and were confirmed by compiling the same operation with GCC on x86-64
  (`-m32` and `-m64`, `-fwrapv` for wrapped results). The tests note where a value is hardware behaviour rather than
  defined C. `data/float_to_integer.txt` holds the results of every floating-to-integer conversion for a set of
  boundary values, generated with gcc-14 `-m64` and `-m32 -msse2 -mfpmath=sse`.
