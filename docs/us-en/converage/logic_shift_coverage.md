# Logic and Shift Coverage

The generated [PTX 9.3 §9.7.8 logic-and-shift
contract](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#logic-and-shift-instructions)
resolves and target-checks the whole named logic-and-shift family: 38 concrete
spellings represented by 37 canonical variants (the `lop3` Boolean-result
variant covers both `.and` and `.or`). This is frontend validation only; it
does not provide execution semantics or simulator support.

`and`, `or`, `xor`, and `not` accept the documented `.pred`, `.b16`, `.b32`,
and `.b64` types. Predicate sources retain PTX predicate constants and
negation. `cnot` accepts `.b16`, `.b32`, and `.b64`.

`lop3.b32` requires a truth-table immediate in `0..255`. Its `.and` and `.or`
layouts, available from PTX 8.2 / SM 70, retain a `d|p` output and predicate
control; only those layouts permit `_` for the data output. Base `lop3` remains
PTX 4.3 / SM 50.

`shf` supports each official `.l/.r` and `.clamp/.wrap` combination at PTX 3.1
/ SM 32. Its count remains a full unsigned 32-bit operand; the frontend does
not incorrectly limit it to the mode's runtime effective range. `shl` supports
`.b16/.b32/.b64`, while `shr` supports the bit, unsigned, and signed 16-, 32-,
and 64-bit spellings. Shift counts are always 32-bit and destination/source
widths must match.
