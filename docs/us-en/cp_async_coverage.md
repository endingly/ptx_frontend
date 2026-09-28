# PTX 9.3 non-bulk `cp.async` coverage

The frontend models PTX ISA 9.3 §9.7.9.26.3 and the legacy mbarrier bridge in
§9.7.14.16.18. Copy completion remains a programmer obligation; the frontend
does not execute asynchronous copies or prove thread scheduling.

| Form | Contract | Availability |
| --- | --- | --- |
| `cp.async.ca.shared{::cta}.global` | `cp-size` is 4, 8, or 16 bytes | PTX 7.0 / SM 80; `::cta` PTX 7.8 |
| `cp.async.cg.shared{::cta}.global` | `cp-size` is 16 bytes | PTX 7.0 / SM 80; `::cta` PTX 7.8 |
| Either copy form with `src-size` | 32-bit register or immediate; known immediate must be less than `cp-size`; remaining destination bytes are zero-filled | Base copy availability |
| Either copy form with `ignore-src` | Predicate register selects full zero fill | PTX 7.5 / SM 80 |
| Either copy form with `.L2::64B/128B/256B` | Typed prefetch-size hint | PTX 7.4 / SM 80 |
| Either copy form with `.L2::cache_hint` | Optional 64-bit cache-policy register; a supplied policy requires the qualifier | PTX 7.4 / SM 80 |
| `cp.async.commit_group`, `cp.async.wait_group`, `cp.async.wait_all` | Existing per-thread group completion syntax | PTX 7.0 / SM 80 |
| `cp.async.mbarrier.arrive{.noinc}{.shared{::cta}}.b64` | Existing CTA-shared mbarrier address and pending-count form | PTX 7.0 / SM 80; `::cta` PTX 7.8 |

The two addresses must resolve to shared destination and global source when
their provenance is known, each aligned to `cp-size`. `src-size` supplied in a
register retains the runtime requirement to be smaller than `cp-size`. The
fourth copy operand retains its meaning as a typed source size, ignore-source
predicate, or cache policy; a cache policy may also follow source control as a
fifth operand. The original three-operand `.ca.shared.global` public variant
retains its `dst`, `src`, and `cp_size` fields.

The `.ca` and `.cg` cache modes and L2 qualifiers are source controls and
performance hints. The frontend checks their shape and availability, not cache
behavior. `cp.async.bulk` and its completion model are separate coverage.
