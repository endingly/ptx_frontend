# Real compiler inputs for the PTX 9.3 audit

This isolated collection has fifteen PTX inputs. It does not
modify the established corpus provenance schema or the SDK support contract.

Twelve are natural NVIDIA nvcc V13.3.33 outputs from four project-authored
ordinary CUDA sources: shared-memory tiled matrix multiplication, a shuffle
reduction with a global atomic, a shared-memory atomic histogram, and a kernel
calling a non-inlined device function with loops and branches. There is no inline
PTX in these sources. Each source is compiled for `sm_80 -O3`,
`sm_90a -O3 -lineinfo`, and `sm_100 -O0`. Optimization options describe the
requested compiler flags, not an assertion about every internal optimization.
The sources and their generated PTX use the repository's MIT license.

Two are raw matrix multiplication outputs from NVIDIA/cuda-samples v12.5,
commit `9c688d7ff78455ed42e345124d1495aad6bf66de`. They identify nvopencc 4.0
(2011), PTX 1.4, and `sm_20, map_f64_to_f32` with different pointer sizes.
Their original bytes and the upstream NVIDIA BSD license are retained in
`upstream/`. Historical compiler output is not presumed to conform to the fixed
PTX 9.3 manual. In particular, these files contain `.loc 16` without `.file 16`;
their version/target combination and map option also conflict with current
fixed-reference profile restrictions. Modern ptxas does not support `sm_20`.
They remain visible as rejected/unsupported observations.

A third upstream output is decoded from the same sample's BSD-licensed
`matrixMul_kernel_32_ptxdump.c` byte array. The exact original C source is
retained. Decoding removes only its terminating C NUL; no PTX text is rewritten.
This distinct NVOPENCC4.0 PTX1.4 / SM10 output has the required `.file` records
and passes the full frontend. Modern ptxas does not support SM10. The companion
upstream `README.TXT` identifies these arrays as embedded nvcc PTX outputs; its
original bytes are also retained. The sample host source carries the same
NVIDIA BSD terms, with pinned URL/hash in the manifest.

`manifest.json` records source/output SHA-256, exact pinned upstream URLs,
compiler flags, and the official CUDA redistribution components and hashes.
The compiler toolchain is not redistributed here. The
[initial audit evidence](../../docs/audits/ptx93_real_inputs/README.md) preserves
the original immediate-store rejection. The
[current follow-up](../../docs/audits/ptx93_st_postfix/README.md) records all
twelve natural outputs passing after the scalar ST extension; fixture bytes
and manifest identities are unchanged.

From the repository root:

```sh
python tools/corpus/ptx93_real_inputs.py check --nvcc /tmp/ptx93-nvcc/bin/nvcc
python tools/corpus/ptx93_real_inputs.py regenerate --nvcc /tmp/ptx93-nvcc/bin/nvcc
python tools/corpus/ptx93_real_inputs.py run \
  --frontend /tmp/ptx93-audit-build/ptx93_probe \
  --ptxas /tmp/ptx93-nvcc/bin/ptxas \
  --output /tmp/ptx93-real-results.json
```

`check` recompiles and compares exact bytes and manifest hashes. `regenerate`
updates natural PTX and its output hashes; it does not download or rewrite the
upstream inputs. The lineinfo outputs retain absolute `.file` paths, so exact
regeneration requires repository `/workspace/ptx_frontend` and compiler prefix
`/tmp/ptx93-nvcc`. This is recorded explicitly in the generation environment.
Other locations can regenerate their own path-specific outputs; do not mistake
those path differences for a compiler semantic regression.

To reproduce the compiler prefix, fetch the four `toolchain.components` archive
URLs relative to `https://developer.download.nvidia.com/compute/cuda/redist/`,
verify each SHA-256, then extract each archive under a single user-owned prefix
with its one top-level archive directory stripped. No full toolkit installation,
GPU, root privileges, or runtime kernel execution is required.

`run` invokes the full parse + `resolveAndValidateModule` probe on each whole
module, then ptxas for its declared target. It records complete stdout/stderr,
return codes, tool/source hashes, and cubin hashes (temporary cubins are removed).
Executable names resolve through `PATH`; missing executables produce a clear error.
Each subprocess has a 60-second timeout, recorded separately as `timed_out`
(and assembler outcome `timed_out`) with captured partial diagnostics.
Its process exit status reports successful evidence collection; inspect the
per-fixture results for frontend or assembler failures. Root counts are only
descriptive lexical summaries, never acceptance or coverage proofs.
