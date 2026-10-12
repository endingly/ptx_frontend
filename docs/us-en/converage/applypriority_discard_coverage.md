# PTX 9.3 `applypriority` and `discard` coverage

The frontend supports both documented spellings of the L2 cache-range
operations in PTX ISA 9.3 §§9.7.9.17–18. Both require PTX 7.4, SM 80, a
128-byte-aligned address, and the immediate size `128`.

| Operation | Generic spelling | Explicit-global spelling |
| --- | --- | --- |
| Normal eviction priority | `applypriority.L2::evict_normal [a], 128` | `applypriority.global.L2::evict_normal [a], 128` |
| Discard range | `discard.L2 [a], 128` | `discard.global.L2 [a], 128` |

The omitted `.global` suffix uses generic addressing. Its runtime address must
fall in the global window; otherwise PTX defines the behavior as undefined.
The frontend accepts an address register with unknown provenance and rejects a
known non-global symbol. Both spellings require provable 128-byte alignment
when the address is statically known. These instructions express cache behavior;
the frontend checks their syntax and typed operands without executing them.
