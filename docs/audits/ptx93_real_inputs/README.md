# Real-input audit evidence

> **Historical snapshot before scalar ST immediate support.** The separately
> authorized extension now accepts all twelve natural compiler outputs; see
> the [current follow-up](../ptx93_st_postfix/README.md). This directory retains
> the original rejection evidence and minimization unchanged.

The rebuilt frontend accepts eleven of twelve fresh ordinary CUDA compiler
outputs in the fifteen-fixture vendored collection. NVIDIA ptxas V13.3.33
accepts all twelve. The remaining natural input exposes a compiler/assembler
extension beyond the fixed manual: `histogram_sm_100.ptx`, compiled at `-O0`,
contains five `st.shared.b32 [register], 0` instructions. The frontend rejects
them during resolution because their source is immediate. PTX 9.3 section
9.7.9.11 explicitly says: “Operand b must be in the .reg state space.” Therefore
assembler acceptance alone does not justify changing the fixed-reference
frontend contract. The unchanged natural output remains in the corpus.

`st_immediate_minimized.ptx` is an explicitly handwritten audit minimization,
not a natural fixture. Its JSON companion records frontend rejection,
ptxas acceptance, cubin hash, and the exact manual reference. This is a
compiler/manual discrepancy observation, not an adopted support extension.

Both pinned NVIDIA matrix outputs parse but fail binding with forty missing
debug-file references each (`.loc 16` without `.file 16`). Their historical
PTX1.4 / SM20 / map_f64_to_f32 combination also violates fixed-reference profile
restrictions; the observed frontend run stops at binding. Modern ptxas rejects
the command because SM20 is unsupported, which is recorded as an unsupported
target rather than an invalid-PTX verdict.

The third vendored upstream input provides a whole-module accepted control:
`matrixMul_kernel_32_embedded.ptx`, decoded without textual edits from the
BSD-licensed original `matrixMul_kernel_32_ptxdump.c` in the same pinned NVIDIA
sample. This distinct SM10 output has declared debug files and passes full
parse/resolve/validation. Modern ptxas reports SM10 unsupported. The original C
array and companion `extras/README.TXT` are retained with source hashes and
license provenance in the manifest.

A bounded additional external search found another accepted control:
GPUOcelot's archived CUDA3.2 `vectorAdd.ptx`, nvopencc3.2 (2010), PTX1.4,
SM10 with map_f64_to_f32. It passes full parse/resolve/validation; ptxas13.3
cannot assemble SM10. `nonvendored_external_observation.json` records its pinned
URL, exact hash and complete commands/results. Its bytes are not redistributed:
the companion NVIDIA source refers to a historical EULA, so the repository root
BSD license was insufficient evidence of redistribution permission for this
sample. This observation is separate from the fifteen reproducible vendored
fixtures and makes no runtime or physical-ABI claim.

`results.json` contains every complete-module frontend result, every ptxas
return code and diagnostic, supported cubin hashes, and the exact probe binary,
checker source, runner, manifest hashes and base commit.
The canonical [probe source](../ptx93_probes/probe.cpp) is identified by path
and SHA-256 in `generation_environment.json`; no duplicate source is maintained. The probe executes
`PtxSyntaxParser::parseModule` and `resolveAndValidateModule`, not an opcode-name
scanner. `generation_environment.json` identifies the exact compiler executable,
version, host GCC, source probe hash, repository directory and compiler prefix.
The manifest pins the official downloaded component hashes. Output `.file`
paths are preserved, including original absolute paths in the lineinfo profile.

Executed checks:

```sh
python tools/corpus/ptx93_real_inputs.py regenerate --nvcc /tmp/ptx93-nvcc/bin/nvcc
python tools/corpus/ptx93_real_inputs.py check --nvcc /tmp/ptx93-nvcc/bin/nvcc
python tools/corpus/ptx93_real_inputs.py run --frontend /tmp/ptx93-audit-build/ptx93_probe --ptxas /tmp/ptx93-nvcc/bin/ptxas --output docs/audits/ptx93_real_inputs/results.json
```

Exact-byte recompilation passed for all twelve natural outputs; all fifteen
manifest fixture hashes and all source/license hashes passed. This finite set
tests varied whole compiler modules, including shared memory, loops, branches,
barriers, atomics, shuffle, function calls and line information. It does not
establish support for all real-world PTX, perform GPU execution, prove dynamic
synchronization, or expand `docs/sdk_support.md`.

Runner safeguards were checked for PATH-based executable resolution, clear
missing-tool errors, and distinct timeout reporting with retained partial output.
The final fifteen-case batch uses the same unchanged checker source and binary;
all fixture/source hashes were rechecked after refreshing the runner identity.
