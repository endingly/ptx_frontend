# Integer Arithmetic Coverage

The frontend models all 161 documented syntax forms in PTX ISA 9.3 §9.7.1:
`add`, `sub`, `mul`, `mad`, `clmad`, `mul24`, `mad24`, `sad`, `div`, `rem`,
`abs`, `neg`, `min`, `max`, `popc`, `clz`, `bfind`, `fns`, `brev`, `bfe`,
`bfi`, `szext`, `bmsk`, `dp4a`, and `dp2a`. The canonical source is
`python/code_gen/resources/ptx_spec/arithmetic.yaml`; parser descriptors,
Resolved IR, and target-aware checking are generated from it.

The normative reference is the [archived CUDA 13.3 PTX ISA 9.3
manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html).
This is frontend syntax and validation coverage, not execution semantics. Form
counts are: add 16, sub 11, mul 16, mad 17, clmad 2, mul24 4, mad24 5, sad 6,
div 6, rem 6, abs 3, neg 4, min 13, max 13, popc 2, clz 2, bfind 8, fns 1,
brev 2, bfe 4, bfi 2, szext 4, bmsk 2, dp4a 4, and dp2a 8.

The model retains the existing full MUL and bit-operation public variants and
appends the remaining forms. `clmad` requires PTX 9.3 and `sm_80`; `fns`
requires PTX 6.0 and `sm_30`; `szext` and `bmsk` require PTX 7.6 and `sm_70`;
and `dp2a`/`dp4a` require PTX 5.0 and `sm_61`. Packed add/sub/min/max/neg
forms retain their documented `sm_90` and `sm_120f` availability.

`fns` checks immediate `base` in `0..31`; register values remain dynamic.
`bmsk` accepts the full 32-bit position and width domain because the manual
defines clamp and wrap behavior for larger values. `szext` likewise accepts a
full unsigned width control. The dot-product forms keep each packed input's
signedness and make destination/accumulator `.u32` only when both inputs are
unsigned, otherwise `.s32`.

The canonical database grows from 72 opcodes and 406 variants to 81 opcodes
and 476 variants. All names and indices of the preceding 72 opcode variants
remain stable. The reused Debug generated subtree measures 13 files, including
9 C++ sources, at 18,444,254 bytes and 381,078 C++ lines; this is an increase
of 1,611,840 bytes and 20,801 C++ lines over the retained baseline. Direct
regeneration into a temporary output completed in 49.885 seconds.

The final Debug build of `test_resolved_ir` with `-j 4` completed 75 Ninja
actions in 69.733 seconds, using an existing configured tree and compiler
cache; this is not a clean-build benchmark. Ordinary Debug CTest passed
884/884 in 11.96 seconds; the installed public package consumer passed 1/1
in 85.75 seconds. Full Python discovery passed 221 tests in 56.646 seconds.
Release validation remains assigned to CI and was not run locally for this change.

The checked-in [CUDA 13.1 `ptxas` evidence](../integer_arithmetic_ptxas.json)
exercised 25 modules: 11 accepted, 10 rejected at the intended contract
boundary, and 4 unavailable because that toolchain supports PTX only through
9.1. Normative PTX 9.3 text governs the FNS immediate bound even where the
older assembler accepts an undefined-behavior input.
