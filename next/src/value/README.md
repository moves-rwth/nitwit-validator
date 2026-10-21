# Module `value`

Run-time values and C arithmetic, shared by constant evaluation in `sema` and by the interpreter.

- `value.hpp`: `Value` (integers up to 128 bits, `float`, `double`, `long double`, byte blobs for records), formats,
  origins, encoding to and from object representations.
- `arithmetic.hpp`: integer and floating operations, conversions, bit-fields. Undefined behaviour is reported in the
  result, never trapped on.

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
  defined C.
