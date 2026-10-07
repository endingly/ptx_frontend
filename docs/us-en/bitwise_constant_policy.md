# Bitwise constant-expression signedness

Binary `&`, `|`, and `^` use the usual arithmetic conversions: their result is
signed when both inputs are signed, and unsigned otherwise. Unary `~` remains
unsigned. This policy affects subsequent comparisons and right shifts; it does
not change the 64-bit payload produced by the bitwise operation itself.

## Specification conflict and decision

[PTX ISA 9.3 section 4.5.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constant-expression-evaluation)
specifies usual arithmetic conversions for binary bitwise operators, whereas
[the summary table in section 4.5.6](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#summary-of-constant-expression-evaluation-rules)
lists unsigned operands/results. The frontend follows the detailed prose,
supported by the offline assembler observations below, rather than the table.
This is an explicit interpretation of inconsistent documentation, not a claim
that NVIDIA has issued a specification correction.

## Reproduction

Run from the repository root, choosing an output path outside the source tree:

```sh
ptxas --version
ptxas -arch=sm_80 submod/semantic/test/fixtures/bitwise_signedness.ptx -o /tmp/bitwise_signedness.cubin
readelf -x .nv.global.init /tmp/bitwise_signedness.cubin
```

Observed on 2026-09-12 with NVIDIA CUDA 13.1, `ptxas V13.1.115`, Linux,
PTX source version 9.1 and target `sm_80`, with no additional options:

| Fixture expression | Emitted little-endian 64-bit value |
| --- | --- |
| `((-1 & -1) < 0)` | `0000000000000001` |
| `((-1 \| 0) < 0)` | `0000000000000001` |
| `((-1 ^ 0) < 0)` | `0000000000000001` |
| `((-4 \| 0) >> 1)` | `fffffffffffffffe` |
| `(((.s64)(-4 \| 0)) >> 1)` | `fffffffffffffffe` |
| `((-1 & -1U) < 0)` | `0000000000000000` |
| `((~0) < 0)` | `0000000000000000` |
| `((-1 + 0) < 0)` | `0000000000000001` |

The complete fixture keeps all eight globals in one visible array. The C++
regression additionally checks signedness before final storage conversion,
including mixed-signedness cases for each binary operator.

These are emitted object bytes, not GPU execution/readback results. The installed
assembler does not support PTX 9.3; this observation does not assert equivalence
across all toolchain versions. No conclusion about `.b128` high-word widening or
physical memory behavior follows from this 64-bit experiment.
