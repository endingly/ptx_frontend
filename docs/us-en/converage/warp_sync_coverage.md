# PTX 9.3 shuffle and vote coverage

The frontend models the documented `shfl.sync`, `vote.sync`, and the legacy shuffle and vote
forms below in PTX ISA 9.3 §9.7.9.6 and §9.7.14.10. It parses and retains the selected operation,
operand layout, and source-predicate negation, then checks local operand types
and target availability. It does not execute warp operations or prove runtime
member participation and convergence.

| Instruction | Supported forms | Destination | Source and mask | Availability |
| --- | --- | --- | --- | --- |
| `shfl.sync` | `.up.b32`, `.down.b32`, `.bfly.b32`, `.idx.b32` | 32-bit-compatible register `d`, optionally paired as `d|p` with a `.pred` register | 32-bit-compatible register `a`; register or immediate `.u32` controls `b`, `c`, and `membermask` | PTX 6.0 / SM 30 |
| `shfl` | `.up.b32`, `.down.b32`, `.bfly.b32`, `.idx.b32` | 32-bit-compatible register `d`, optionally paired as `d|p` with a `.pred` register | 32-bit-compatible register `a`; register or immediate `.u32` controls `b` and `c`; no membermask | PTX >=3.0, SM >=30, and (PTX <6.4 or SM <70) |
| `vote.sync` | `.all.pred`, `.any.pred`, `.uni.pred` | `.pred` register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote.sync` | `.ballot.b32` | 32-bit-compatible register `d` | `.pred` register `a` or `!a`; register or immediate `.u32` `membermask` | PTX 6.0 / SM 30 |
| `vote` | `.all.pred`, `.any.pred`, `.uni.pred` | `.pred` register `d` | `.pred` register `a` or `!a`; no membermask | PTX 1.2 / exact generic `sm_12`, `sm_13`, `sm_20`, `sm_30`, `sm_32`, `sm_35`, `sm_37`, `sm_50`, `sm_52`, `sm_53`, `sm_60`, `sm_61`, `sm_62` |
| `vote` | `.ballot.b32` | 32-bit-compatible register `d` | `.pred` register `a` or `!a`; no membermask | PTX 1.2 / exact generic `sm_20`, `sm_30`, `sm_32`, `sm_35`, `sm_37`, `sm_50`, `sm_52`, `sm_53`, `sm_60`, `sm_61`, `sm_62` |

The shuffle predicate output is optional. A paired output requires both
registers; `_` is not accepted in either half, and negating the output predicate
is invalid. The vote source may be negated in every supported mode, while the
destination must be a register of the form's type. Synchronized forms require
the member mask; legacy shuffle and vote forms reject an extra mask and have no `.sync`.
The distinct public shuffle forms `ShflUpB32`, `ShflDownB32`, `ShflBflyB32`, and
`ShflIdxB32` retain the selected bare or paired destination, data source, b/c
controls, and owned source ranges. Their optional `dst_register` and
`dst_shfl_destination` fields preserve which destination layout was written;
paired data and predicate results use the existing typed destination path.
No sync or membermask field is synthesized. Legacy shuffle requires PTX >=3.0
and SM >=30, with PTX <6.4 or SM <70. Source targets must still be recognized
catalog profiles with legal introduction versions; uncatalogued profiles such
as `sm_63` remain unsupported. Deprecation from PTX6.0 does not reject the form.
The four distinct public forms `VoteAllPred`, `VoteAnyPred`, `VoteUniPred`, and
`VoteBallotB32` retain predicate negation and owned source ranges without
fabricating sync or mask fields. Their availability is limited to the exact
supported catalog profiles listed above, not all historical GPU profiles. They
reject `sm_70` and newer targets, including the historical PTX < 6.4 / `sm_70`
case outside this supported scope.

Legacy SHFL is deprecated from PTX6.0, which does not reject it. Removal from PTX6.4 applies only at SM70 or newer: PTX6.3/sm75 remains accepted, PTX6.4/sm75 rejects, and PTX9.3/sm62 remains accepted. Source profile introduction rules still apply; numeric SM69 is a checker unit boundary, not a source target. The four modes and two destination layouts keep their existing carrier, control and sink rules; synchronized forms and the separate vote target lists are unchanged.
