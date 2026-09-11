# Register declaration compatibility

## Contract

`register_width: same_width` checks both width and fundamental-type
compatibility. It is not enum identity: bit-size registers can carry compatible
same-width integer or floating operands, and signed/unsigned integer declarations
can be interchanged at the same width. Integer/float substitutions remain errors.
`mul.wide.u32` and `mad.wide.u32` still require 32-bit multiplicands and a 64-bit
result (and 64-bit addend for `mad`). No wider-register relaxation is introduced.

`exact` retains its existing enum-identity meaning. It is appropriate for
explicit declaration-format requirements, not ordinary arithmetic merely because
its width is fixed. `equal_or_wider` and immediate conversion are unchanged.
Resolution still builds the same IR; generated checking applies the descriptor
policy to bound register declarations. Resolving a module alone is not a claim
that its instructions pass checking.

## Normative source ledger

The project specification is PTX ISA 9.3. This correction uses the
[CUDA 13.3.0 archived PTX ISA 9.3 manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html),
retrieved 2026-09-10: sections 5.2.1 and 9.4/Table 26 for fundamental
compatibility; 9.7.1.3/9.7.1.4 for wide integer operands; 5.2.3/5.2.5 for
alternate and packed declaration formats. The moving unversioned manual is not
used as a frozen edition.

## Verification boundary

The audit starts from `c6e5e2d5503b6f8a7d01dca7e7cb43e33f78719a`:
106 explicit `exact` declarations across three specification files. The scoped
decisions are:

| Consumers | Policy decision |
| --- | --- |
| `mul/mad.wide.u32`, `popc/clz`, `bfind/bfe`, ordinary `mad/div/min/max` float forms | Same-width fundamental compatibility; keep each operand's original width |
| `neg.f16x2`, packed `cvt` destination | Require expected `f16x2` with `same_width`, accepting `.f16x2`/`.b32`, not arbitrary 32-bit storage |
| `cvt` ordinary f32 operands, `mapa/getctarank`, `isspacep` | Same-width compatibility; address shape/state-space checks remain independent |
| `vote/match/redux/elect/activemask`, mbarrier counts/parity/hints, decoded cluster CTA IDs | Same-width compatibility; masks, ranges, and target checks remain independent |
| Alternate-format FMA (`bf16`, `bf16x2`, `f32x2`, mixed bf16 sources) | Retain exact bit-container requirements |
| Opaque mbarrier state and cluster cancellation response | Retain exact token/response storage and shape requirements |
| Mbarrier opaque report value | Retain existing exact `.b8` model; the audited manual does not independently establish that declaration width |

This is not a full audit of wider-register `cvt` behavior. The affected f32
operands now accept compatible **same-width** declarations; further widening
under PTX section 9.4.1 and the `cvt` instruction rules is outside this correction.

The regression suite links the real parser, resolver, compatibility helper, and
generated checker. An independent type matrix and declaration-substitution tests
do not obtain their expected answers from the instruction YAML. Negative cases
check diagnostic kinds and operand ranges; descriptor assertions verify policy
propagation without changing public IR fields.

The original round-seven evidence bundle is not a prerequisite for these new
regressions: reconstructed cases must not be reported as execution of an
unavailable attachment. The bounded workspace search found no original bundle.
The initial non-interactive PATH lookup did not locate `ptxas`. A subsequent
interactive-shell check found `/usr/local/cuda/bin/ptxas`, supplied through
`.bashrc`: CUDA 13.1, V13.1.115, build
`cuda_13.1.r13.1/compiler.37061995_0`.

The assembler and the current linked frontend were run on the **same 23
reconstructed modules**, using `.version 8.0`, `.target sm_80` and
`ptxas -arch=sm_80 -O0`: both accepted 15 and rejected 8, with complete
accept/reject agreement. The assembler produced no warnings; rejected modules
reported argument-mismatch errors and exit 255. Frontend rejections were
generated-checker diagnostics propagated by module resolution, not parse failures.
[Full sources, driver, exit codes, and diagnostics](../register_type_policy_ptxas.json)
record wide mul/mad, bit counts, ordinary floating and bit operations, masks,
packed f16x2/bf16 controls, and the issue's example kernel.

This is a CUDA 13.1 comparison of those PTX 8.0 fixtures, **not** a CUDA 13.3 /
PTX 9.3 toolchain validation or a complete exact-consumer conformance audit.

Validation on 2026-09-10 used GCC 15.2.0 and Python 3.14.4. Before changing
the specifications, the initial six linked regression groups produced five
failures and one passing exact-bf16 control. After the correction, all 506
resolved-IR tests (including nine register-policy groups), nine modern-operand
code-generation tests, and 199 Python model/generator tests passed. Commands:

```sh
cmake --build out/build/ci-linux-gcc-debug --target test_resolved_ir test_modern_operand_codegen -j 4
out/build/ci-linux-gcc-debug/submod/resolved_ir/test_resolved_ir --gtest_brief=1
out/build/ci-linux-gcc-debug/submod/resolved_ir/test_modern_operand_codegen --gtest_brief=1
PYTHONPATH=python .venv/bin/python -m unittest discover -s python/tests -t python -p 'test_*.py' -q
```
