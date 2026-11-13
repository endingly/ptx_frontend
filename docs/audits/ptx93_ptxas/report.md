# Finite PTX 9.3 comparison with NVIDIA ptxas

This findings-only check assembled 30 complete fixtures and ran the same bytes
through the existing full-frontend audit driver (parse + resolveAndValidate).
There is one confirmed acceptance-difference family on a target supported by
both: whole hardware-vector 16-bit MOV in PTX 9.3 targeting `sm_80` is
accepted by ptxas and rejected by the frontend (six concrete cases). No frontend, specification, support-contract or
existing-test changes were made.

## Tool provenance and reproduction

The official CUDA 13.3.0 redistribution manifest identifies cuda_nvcc component
13.3.33. Its 31,609,400-byte archive SHA256 was verified as
`93b098bda4a562ebf3541523ce82adc43f106a81dcf28bcbf8f0d8e093d1c66f`.
Only `bin/ptxas` and the accompanying license were extracted under
`/tmp/ptx93-ptxas`; no system installation was performed.

The actual executable reports **release 13.3, V13.3.33**, build
`cuda_13.3.r13.3/compiler.37862127_0`, built April 24, 2026. Executable SHA256:
`7fdd01a4cf50e30746da98989c9272a907f491e6fd7fecfda14642e4375f88fb`.
This observed version is independent of historical audit tool-version claims.
Download URLs, manifest hash, archive identity, baseline commit and frontend
driver source hash are in [download_provenance.json](download_provenance.json).
Executable hashes, full `--version`/`--help`, exact commands, fixture hashes,
exit codes, diagnostics, stdout/stderr hashes and temporary cubin hashes/byte
counts are in [results.json](results.json).

From the repository root, after installing that tool and building the existing
[probe driver](../ptx93_probes/probe.cpp):

```sh
python3 docs/audits/ptx93_ptxas/run.py \
  --ptxas /tmp/ptx93-ptxas/cuda_nvcc-linux-x86_64-13.3.33-archive/bin/ptxas \
  --frontend /tmp/ptx93-audit-build/ptx93_probe
```

The runner regenerates all [fixtures](fixtures/) and outcomes. Each ptxas command
uses `-O0`, `--gpu-name` matching the fixture's `.target`, and a temporary output
file. Each process has a 30-second timeout and a 64-KiB-per-stream output bound;
none timed out or produced truncated output. Cubins were hashed and deleted
with their temporary directory; no binary artifacts are retained in the repo.
Modern instruction-result checks store outputs with `st.volatile.global`;
multimem reduction itself has a memory effect. Register declaration checks
also contain an observable store. Historical target probes retain the original
minimal source because their retired targets are unavailable to this tool.

## Observed results

| Fixture group | ptxas | Full frontend | Interpretation |
| --- | --- | --- | --- |
| Valid kernel / unknown opcode | accept / reject | accept / reject | Positive and negative controls pass. |
| Single `.file` / repeated example index / identical repeated index | accept / reject / reject | same | Both duplicate forms report a duplicate file index/symbol; examples do not establish replacement behavior. |
| `%pm0_64`, `%pm7_64`: `mov.u32` / `mov.u64` | reject / accept | same | U32 examples conflict with the declared U64 type and actual assembler checking. |
| `bfe.b32` / `bfe.u32` / `bfe.s32` | reject / accept / accept | same | Assembler follows the documented signed/unsigned type domain. |
| `bmsk.wrap` width 2 / 33; `bmsk.clamp` width 32 / 33 | all accept | all accept | Width note is not enforced as an immediate rejection condition; runtime mask values were not measured. |
| `multimem.red.relaxed.gpu.max.f64` / `.add.f64`, `sm_90` | reject / accept | same | `.max.f64` specifically diagnosed as incorrect type for operation; valid floating add control rules out general target/layout failure. |
| Historical PTX 1.0 U16 whole `%tid`, `sm_10` | unsupported target | reject | Modern ptxas cannot adjudicate this historical input. |
| Historical PTX 2.0 U32 whole `%tid`, `sm_20` | unsupported target | accept | Tool support limitation, not an assembler language rejection. |
| Current PTX 9.3 U16 whole `%tid`, `sm_80` | **accept** | **reject** | Confirmed compatibility difference; see below. |
| Current PTX 9.3 U16 whole `%ntid`, `%ctaid`, `%nctaid`; S16/B16 whole `%tid`, `sm_80` | all accept | all reject | Same whole-vector compatibility difference, five bounded extensions. |
| Current PTX 9.3 U32 whole `%tid`, `sm_80` | accept | accept | Matching whole-vector control. |
| Register `.v2 .f64` / `.v4 .f64` / `.v2 .pred` | accept / reject / reject | same | Both enforce 128-bit non-predicate width and scalar predicate declarations. |

For [tid-v9-3-u16.ptx](fixtures/tid-v9-3-u16.ptx), ptxas exits 0 with no
diagnostics and produces a 3,368-byte cubin (hash recorded in JSON). The
frontend exits 3 with `Checking/InvalidVectorOperand`:
`Whole hardware vector MOV requires a four-lane intrinsic u32 source.`
The fixture declares `.reg .v4 .u16 values`, moves the whole `%tid` vector,
then stores all four lanes through a parameter-derived pointer. The bounded extensions also assemble successfully with U16 whole-vector reads
from `%ntid`, `%ctaid`, `%nctaid` and S16/B16 whole-vector reads from `%tid`.
The frontend rejects all five with the same vector-source diagnostic. The matching
U32 fixture changes both register/instruction/store widths and passes both
tools. This is a reproducible current-tool compatibility gap. It does not by
itself settle whether the manual's explicit backward-compatibility clause
for *component* accesses prescribes every whole-vector spelling, or change
the repository's maintained SDK support contract. Authority review owns that
interpretation; the source and raw evidence are preserved here.

## Limits

These are finite hand-selected comparison cases, not fuzzing, exhaustive ISA
coverage or GPU execution. Successful assembly establishes tool acceptance,
not runtime semantics, performance-counter availability or the validity of a
runtime multimem address. Modern PTX 9.3/sm_80 and sm_90 controls successfully
assembled, ruling out unsupported-version/target explanations for the principal
comparisons. Retired sm_10 and sm_20 are explicitly outside this assembler's
supported `--gpu-name` values. No link/layout failures were observed.
