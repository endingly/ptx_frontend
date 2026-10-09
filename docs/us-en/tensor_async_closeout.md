# Tensor-map and TMA frontend closeout matrix

This matrix audits the M14-I01–I22 and M14-C01–C03 requirements against the
fixed [CUDA 13.3 PTX ISA 9.3][ptx] and this source tree. “Covered” means that a
supported source form can be parsed without losing its spelling or locations,
resolved to owned typed IR, and checked against its instruction-local operands
and selected PTX/target profile. The tensor map remains an **opaque 128-byte
object**. Neither the selected instruction nor caller-supplied
[`TensorMapKnownFacts`][facts-doc] authenticates its bytes. The matrix does not
claim GPU execution, runtime synchronization, or complete coverage of every
form in the PTX instruction family.

The primary #152 slice is tensor-map and tensor-copy forms. The non-tensor bulk
forms and their completion operations are shared dependencies delivered under
#151; `red.async` belongs to the atomic/reduction family. Their rows are included
because the roadmap lists them under M14, without reassigning ownership. The
exact supported forms and adjacent rejected forms are in the linked schema and
tests; a row does not authorize an unlisted suffix, mode, or target combination.

| Roadmap | PTX 9.3 requirement | Source contract | Executable evidence | Ownership |
| --- | --- | --- | --- | --- |
| M14-I01 | [§5.5.1 dimension/format][dimension], [§5.5.2 access mode][modes], [§5.5.8 tensor map][map] | [typed rank/mode and Table 33 domains][ir]; [conditional element/swizzle/interleave facts][facts] | [replacement fields][replace-test], [known-facts rules][facts-test] | #152, descriptor values conditional |
| M14-I02 | [§5.5.8 map operand][map] | [owned `ResolvedTensorMapRef` address, identity and range][ir]; [selected forms][schema] | [parameter/symbol/register descriptor cases][tensor-test] | #152 |
| M14-I03 | [§9.7.9.26.5.2 coordinates][tensor-copy] | [owned coordinates and source ranges][ir]; [rank/mode binding][model] | [CST/AST composite][ast-test], [rank/sign conversion][tensor-test] | #152 |
| M14-I04 | [§9.7.9.27 `tensormap.replace`][replace] | [replacement field/code domain][ir], [type/target checker][checker] | [field, value, target and tamper cases][replace-test] | #152 |
| M14-I05 | [§9.7.14.17 `tensormap.cp_fenceproxy`][proxy] | [proxy form][schema], [scope/size checker][checker] | [proxy scope, operand and tamper cases][replace-test] | #152 |
| M14-I06 | [§9.7.9.26.4.1 global→cluster copy][bulk-copy] | [non-tensor bulk forms][schema], [address/completion checker][checker] | [copy and group topology][bulk-test] | #151 shared |
| M14-I07 | [§9.7.9.26.4.1 CTA→global copy][bulk-copy] | [directional bulk forms][schema], [address/completion checker][checker] | [copy and group topology][bulk-test] | #151 shared |
| M14-I08 | [§9.7.9.26.4.1 `.sem/.scope`][bulk-copy] | [exact qualifiers and availability][schema] | [qualifier matrix and invalid pairings][bulk-test] | #151 shared |
| M14-I09 | [§9.7.9.26.4.2 bulk reduction][bulk-reduce] | [operation/type and completion forms][schema] | [reduction/scope and negative cases][bulk-test] | #151 shared |
| M14-I10 | [§9.7.9.26.4.3 bulk prefetch][bulk-prefetch] | [L2/global prefetch forms][schema] | [reduction and prefetch cases][bulk-test] | #151 shared |
| M14-I11 | [§9.7.9.26.5.2 tiled load][tensor-copy] | [rank 1–5 shared destinations and mbarrier forms][schema]; [typed mode][ir] | [all ranks, direction and pointer checks][tensor-test] | #152 |
| M14-I12 | [§9.7.9.26.5.2 tiled store][tensor-copy] | [rank 1–5 global destinations and bulk-group forms][schema] | [store ranks and signed-coordinate checks][tensor-test] | #152 |
| M14-I13 | [§9.7.9.26.5.3 tensor reduction][tensor-reduce] | [selected op/rank/mode identities][schema]; [typed reduction metadata][model] | [identity, element-type and target cases][reduction-test] | #152 |
| M14-I14 | [§9.7.9.26.5.4 tensor prefetch][tensor-prefetch] | [rank 1–5 forms][schema], [owned tensor operand][ir] | [prefetch rank and tile provenance][tensor-test] | #152 |
| M14-I15 | [§9.7.9.26.5.2 im2col][tensor-copy] | [im2col and no-offset modes, U16 info roles][model] | [info arity/type][im2col-test], [no-offset forms][nooffset-test] | #152 |
| M14-I16 | [§9.7.9.26.5.2 W/W128][tensor-copy] | [W halo/offset and exact target availability][model] | [W-family target endpoints and negative forms][im2col-test] | #152 |
| M14-I17 | [§9.7.9.26.5.2 gather/scatter][tensor-copy] | [four-row role and rank-two modes][ir]; [forms][schema] | [four owned forms and adjacent rejections][gather-test] | #152 |
| M14-I18 | [§9.7.9.26.6.1 bulk commit][commit] | [distinct `bulk_group` completion kind][completion]; [form][schema] | [copy/group topology][bulk-test] | #151 shared |
| M14-I19 | [§9.7.9.26.6.2 bulk wait][wait] | [constant count and optional `.read`][schema] | [copy/group topology and invalid forms][bulk-test] | #151 shared |
| M14-I20 | [§9.7.9.12 `st.async`][store-async] | [shared mbarrier and global release forms][schema] | [store topology and destination-base cases][bulk-test] | #151 shared |
| M14-I21 | [§9.7.9.14 `st.bulk`][store-bulk] | [zero-fill size/type/target forms][schema] | [store size/version and owned binding cases][bulk-test] | #151 shared |
| M14-I22 | [§9.7.14.7 `red.async`][red-async] | [shared completion and global release forms][red-schema] | [async reduction modes and negative forms][atomic-test] | atomic/reduction shared |
| M14-C01 | [§9.7.9.26.6 completion][completion-ptx] | [one `AsyncCompletionKind` domain][completion] distinguishes async-group, bulk-group and mbarrier complete-tx-bytes; instruction forms select a value | [bulk topology][bulk-test], [tensor direction][tensor-test] | #151/#152 shared |
| M14-C02 | [§9.7.9.26.5.1 restrictions][restrictions] | [rank/mode/coordinate/direction/space checker][checker]; [conditional descriptor-facts query][facts] | [tensor][tensor-test], [im2col][im2col-test], [known facts][facts-test] | #152, opaque boundary |
| M14-C03 | [§9.7.9.26.4–.6 forms][tensor-copy] | [schema variants and exact target availability][schema] | [sm90a/sm100 positive and adjacent negative C++ cases][tensor-test], [bulk][bulk-test], [mode][im2col-test] | #151/#152 shared corpus |

The #152 frontend slice also includes cross-cutting forms: cluster multicast
masks are covered by the [schema][schema], [typed mask][ir], and
[positive/negative cases][multicast-test]; per-instruction `.cta_group::1/::2`
routing is covered by its [typed role][ir] and
[mixed-group/target cases][group-test]; tiled/reduction/store
`.im2col_no_offs` is covered by [mode tests][nooffset-test]; canonical
`.L2::cache_hint` and optional final B64 policy are covered by the
[schema][schema], [owned query][cache], and
[positive/negative/tamper cases][cache-test]. Their precise clauses are
[§9.7.9.26.5.2][tensor-copy], [§9.7.9.26.5.3][tensor-reduce], and
[§9.7.9.26.5.4][tensor-prefetch].

The source-level regression tests above cover selected forms and adjacent
negative boundaries; they are not a proof that every possible instruction
combination assembles. The [26 recorded PTXAS 13.3.73 probes][ptxas] are a
bounded cache-control experiment, separate from the C++ corpus. They expose a
hint-only discrepancy between the formal PTX syntax and that assembler, and do
not demonstrate runtime cache effects.

Static checking can establish known address space/alignment, operand shape,
literal conversion and exact selected target availability. The
[22 caller-known-facts rule families][facts-doc] can conditionally check supplied
descriptor claims, including some bounds and swizzle-dependent alignment
relationships; missing facts remain `Unresolved`. Without sufficient supplied
facts or runtime values, raw descriptor fields, coordinates, peer/mask
membership, barrier locality/completion, bounds, swizzle-dependent alignment
and actual memory effects remain unproven. A full raw 128-byte decoder needs a
separately specified byte layout and is not part of this closeout.

[ptx]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html
[dimension]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensor-dimension-size-format
[modes]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensor-access-modes
[map]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensor-tensormap
[restrictions]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-tensor-copy-restrictions
[tensor-copy]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-tensor
[tensor-reduce]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-reduce-async-bulk-tensor
[tensor-prefetch]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-prefetch-tensor
[bulk-copy]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk
[bulk-reduce]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-reduce-async-bulk
[bulk-prefetch]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-prefetch
[commit]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-commit-group
[wait]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-cp-async-bulk-wait-group
[completion-ptx]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-bulk-tensor-copy-completion
[replace]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-tensormap-replace
[proxy]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-tensormap-cp-fenceproxy
[store-async]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st-async
[store-bulk]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#data-movement-and-conversion-instructions-st-bulk
[red-async]: https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#parallel-synchronization-and-communication-instructions-red-async
[schema]: ../../instructions/ptx_spec/data_movement_and_conversion.yaml
[red-schema]: ../../instructions/ptx_spec/parallel_synchronization_and_communication.yaml
[ir]: ../../submod/resolved_ir/include/ptx_resolved_ir_foundation.hpp
[model]: ../../python/src/ptx_frontend/ir/resolved_ir.py
[completion]: ../../python/src/ptx_frontend/spec/model.py
[checker]: ../../submod/resolved_ir/src/ptx_resolved_ir_checker.cpp
[facts]: ../../submod/resolved_ir/include/ptx_tensor_map_known_facts.hpp
[facts-doc]: tensor_map_known_facts.md
[cache]: ../../submod/resolved_ir/include/ptx_tensor_cache_controls.hpp
[ptxas]: ../tensor_cache_controls_ptxas.json
[ast-test]: ../../submod/syntax/test/test_ptx_syntax_ast_parser.cpp
[replace-test]: ../../submod/resolved_ir/test/test_tensormap_replacement.cpp
[tensor-test]: ../../submod/resolved_ir/test/test_tensor_async_coverage.cpp
[reduction-test]: ../../submod/resolved_ir/test/test_tensor_reduction.cpp
[im2col-test]: ../../submod/resolved_ir/test/test_tensor_im2col_info.cpp
[nooffset-test]: ../../submod/resolved_ir/test/test_tensor_no_offsets.cpp
[gather-test]: ../../submod/resolved_ir/test/test_tensor_gather_scatter.cpp
[multicast-test]: ../../submod/resolved_ir/test/test_tensor_multicast.cpp
[group-test]: ../../submod/resolved_ir/test/test_tensor_cta_group.cpp
[cache-test]: ../../submod/resolved_ir/test/test_tensor_cache_controls.cpp
[bulk-test]: ../../submod/resolved_ir/test/test_bulk_async_coverage.cpp
[atomic-test]: ../../submod/resolved_ir/test/test_atomic_reduction_coverage.cpp
[facts-test]: ../../submod/resolved_ir/test/test_tensor_map_known_facts.cpp
