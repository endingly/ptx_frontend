# PTX 9.3 synchronized shuffle and vote coverage

The frontend models the documented `shfl.sync` and `vote.sync` forms in PTX ISA
9.3 §9.7.9.6 and §9.7.14.10. It parses and retains the selected operation,
operand layout, and source-predicate negation, then checks local operand types
and target availability. It does not execute warp operations or prove runtime
member participation and convergence.

| Instruction | Supported forms | Destination | Source and mask | Availability |
| --- | --- | --- | --- | --- |
| `shfl.sync` | `.up.b32`, `.down.b32`, `.bfly.b32`, `.idx.b32` | 32-bit-compatible register `d`, optionally paired as `d|p` with a `.pred` register | 32-bit-compatible register `a`; register or immediate `.u32` controls `b`, `c`, and `membermask` | PTX 6.0 / SM 30 |
| `vote.sync` | `.all.pred`, `.any.pred`, `.uni.pred` | `.pred` register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote.sync` | `.ballot.b32` | 32-bit-compatible register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |

The shuffle predicate output is optional. A paired output requires both
registers; `_` is not accepted in either half, and negating the output predicate
is invalid. The vote source may be negated in every supported mode, while the
destination must be a register of the form's type. The member mask is required
in every form. Bare legacy `shfl` and `vote` instructions are outside this
boundary.
