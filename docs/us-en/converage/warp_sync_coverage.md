# PTX 9.3 shuffle and vote coverage

The frontend models the documented `shfl.sync`, `vote.sync`, and the legacy vote
forms below in PTX ISA 9.3 §9.7.9.6 and §9.7.14.10. It parses and retains the selected operation,
operand layout, and source-predicate negation, then checks local operand types
and target availability. It does not execute warp operations or prove runtime
member participation and convergence.

| Instruction | Supported forms | Destination | Source and mask | Availability |
| --- | --- | --- | --- | --- |
| `shfl.sync` | `.up.b32`, `.down.b32`, `.bfly.b32`, `.idx.b32` | 32-bit-compatible register `d`, optionally paired as `d|p` with a `.pred` register | 32-bit-compatible register `a`; register or immediate `.u32` controls `b`, `c`, and `membermask` | PTX 6.0 / SM 30 |
| `vote.sync` | `.all.pred`, `.any.pred`, `.uni.pred` | `.pred` register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote.sync` | `.ballot.b32` | 32-bit-compatible register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote` | `.all.pred`, `.any.pred`, `.uni.pred` | `.pred` register `d` | `.pred` register `a` or `!a`; no membermask | PTX 1.2 / exact generic `sm_12`, `sm_13`, `sm_20`, `sm_30`, `sm_50`, `sm_52` |
| `vote` | `.ballot.b32` | 32-bit-compatible register `d` | `.pred` register `a` or `!a`; no membermask | PTX 1.2 / exact generic `sm_20`, `sm_30`, `sm_50`, `sm_52` |

The shuffle predicate output is optional. A paired output requires both
registers; `_` is not accepted in either half, and negating the output predicate
is invalid. The vote source may be negated in every supported mode, while the
destination must be a register of the form's type. Synchronized forms require
the member mask; legacy vote forms reject an extra mask and have no `.sync`.
The four distinct public forms `VoteAllPred`, `VoteAnyPred`, `VoteUniPred`, and
`VoteBallotB32` retain predicate negation and owned source ranges without
fabricating sync or mask fields. Their availability is limited to the exact
supported catalog profiles listed above, not all historical GPU profiles. They
reject `sm_70` and newer targets, including the historical PTX < 6.4 / `sm_70`
case outside this supported scope. Bare legacy `shfl` remains outside this boundary.
