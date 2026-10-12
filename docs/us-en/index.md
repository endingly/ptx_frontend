# Detailed reference index

Start with the [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki) for
technical guides and [current scope](converage/coverage_status.md) for evidence boundaries.
These detailed source references retain design decisions, per-family contracts,
and reproducible evidence. Historical audit pins and build measurements describe
their recorded revisions, not current support. Matching English and Chinese
filenames are maintained together; code and tests decide documentation drift.
Design, API, and policy references remain in this language directory. Coverage
records, audits, and closeout matrices are grouped under `converage/`.

## Design and reference

| Detailed topic | English | 中文 |
| --- | --- | --- |
| Bitwise constant-expression signedness | [EN](bitwise_constant_policy.md) | [中文](../zh-han/bitwise_constant_policy.md) |
| Generated-model build scalability | [EN](build_scalability.md) | [中文](../zh-han/build_scalability.md) |
| Installed CMake Components | [EN](cmake_components.md) | [中文](../zh-han/cmake_components.md) |
| C++ naming and legacy-interface policy | [EN](code_conventions.md) | [中文](../zh-han/code_conventions.md) |
| Control-flow Operand Syntax Design | [EN](control_flow_syntax_design.md) | [中文](../zh-han/control_flow_syntax_design.md) |
| Declaration Semantics Design | [EN](declaration_semantics_design.md) | [中文](../zh-han/declaration_semantics_design.md) |
| PTX Lexer Design | [EN](lexer_design.md) | [中文](../zh-han/lexer_design.md) |
| Parameter declarations | [EN](parameter_declarations.md) | [中文](../zh-han/parameter_declarations.md) |
| Python Generator Model Design | [EN](python_generator_model.md) | [中文](../zh-han/python_generator_model.md) |
| Register declaration compatibility | [EN](register_type_policy.md) | [中文](../zh-han/register_type_policy.md) |
| C++ Resolved IR Design | [EN](resolved_ir_design.md) | [中文](../zh-han/resolved_ir_design.md) |
| Resolved storage declarations | [EN](storage_declarations.md) | [中文](../zh-han/storage_declarations.md) |
| Symbol Binding Design | [EN](symbol_binding_design.md) | [中文](../zh-han/symbol_binding_design.md) |
| CST and Syntax AST design | [EN](syntax_ast_design.md) | [中文](../zh-han/syntax_ast_design.md) |
| Tensor async cache controls | [EN](tensor_cache_controls.md) | [中文](../zh-han/tensor_cache_controls.md) |
| Caller-known tensor-map facts | [EN](tensor_map_known_facts.md) | [中文](../zh-han/tensor_map_known_facts.md) |
| YAML Instruction Spec: Structure and Principles | [EN](yaml_instruction_spec.md) | [中文](../zh-han/yaml_instruction_spec.md) |

## Coverage records

| Detailed topic | English | 中文 |
| --- | --- | --- |
| `abs` and `neg` floating-point coverage | [EN](converage/abs_neg_coverage.md) | [中文](../zh-han/converage/abs_neg_coverage.md) |
| Floating and mixed `add`/`sub` coverage | [EN](converage/add_sub_coverage.md) | [中文](../zh-han/converage/add_sub_coverage.md) |
| PTX 9.3 `applypriority` and `discard` coverage | [EN](converage/applypriority_discard_coverage.md) | [中文](../zh-han/converage/applypriority_discard_coverage.md) |
| PTX 9.3 atomic and reduction coverage | [EN](converage/atomic_reduction_coverage.md) | [中文](../zh-han/converage/atomic_reduction_coverage.md) |
| CTA `barrier.sync`, `barrier.arrive`, and `barrier.red` coverage | [EN](converage/barrier_sync_coverage.md) | [中文](../zh-han/converage/barrier_sync_coverage.md) |
| Integer Bit-Operation Coverage | [EN](converage/bit_operations_coverage.md) | [中文](../zh-han/converage/bit_operations_coverage.md) |
| Non-tensor bulk asynchronous data movement | [EN](converage/bulk_async_coverage.md) | [中文](../zh-han/converage/bulk_async_coverage.md) |
| Extended-precision integer coverage | [EN](converage/carry_coverage.md) | [中文](../zh-han/converage/carry_coverage.md) |
| Conversion and Address-Query Coverage | [EN](converage/conversion_coverage.md) | [中文](../zh-han/converage/conversion_coverage.md) |
| Core Opcode Completeness Audit | [EN](converage/core_opcode_completeness_audit.md) | [中文](../zh-han/converage/core_opcode_completeness_audit.md) |
| Current coverage and evidence boundaries | [EN](converage/coverage_status.md) | [中文](../zh-han/converage/coverage_status.md) |
| PTX 9.3 non-bulk `cp.async` coverage | [EN](converage/cp_async_coverage.md) | [中文](../zh-han/converage/cp_async_coverage.md) |
| PTX 9.3 `createpolicy` coverage | [EN](converage/createpolicy_coverage.md) | [中文](../zh-han/converage/createpolicy_coverage.md) |
| Floating `div` coverage | [EN](converage/div_coverage.md) | [中文](../zh-han/converage/div_coverage.md) |
| Fabric/CFT coverage | [EN](converage/fabric_coverage.md) | [中文](../zh-han/converage/fabric_coverage.md) |
| Ordinary `fence` coverage | [EN](converage/fence_coverage.md) | [中文](../zh-han/converage/fence_coverage.md) |
| FMA Coverage | [EN](converage/fma_coverage.md) | [中文](../zh-han/converage/fma_coverage.md) |
| Integer Arithmetic Coverage | [EN](converage/integer_arithmetic_coverage.md) | [中文](../zh-han/converage/integer_arithmetic_coverage.md) |
| LD Coverage | [EN](converage/ld_coverage.md) | [中文](../zh-han/converage/ld_coverage.md) |
| PTX 9.3 `ldu` coverage | [EN](converage/ldu_coverage.md) | [中文](../zh-han/converage/ldu_coverage.md) |
| Logic and Shift Coverage | [EN](converage/logic_shift_coverage.md) | [中文](../zh-han/converage/logic_shift_coverage.md) |
| Floating `mad` coverage | [EN](converage/mad_coverage.md) | [中文](../zh-han/converage/mad_coverage.md) |
| `mbarrier.test_wait` qualifier coverage | [EN](converage/mbarrier_test_wait_coverage.md) | [中文](../zh-han/converage/mbarrier_test_wait_coverage.md) |
| `mbarrier.try_wait` qualifier coverage | [EN](converage/mbarrier_try_wait_coverage.md) | [中文](../zh-han/converage/mbarrier_try_wait_coverage.md) |
| `membar` coverage | [EN](converage/membar_coverage.md) | [中文](../zh-han/converage/membar_coverage.md) |
| Floating `min`/`max` coverage | [EN](converage/min_max_coverage.md) | [中文](../zh-han/converage/min_max_coverage.md) |
| PTX 9.3 miscellaneous instruction coverage | [EN](converage/miscellaneous_coverage.md) | [中文](../zh-han/converage/miscellaneous_coverage.md) |
| MUL Coverage | [EN](converage/mul_coverage.md) | [中文](../zh-han/converage/mul_coverage.md) |
| Multimem frontend coverage (PTX ISA 9.3) | [EN](converage/multimem_coverage.md) | [中文](../zh-han/converage/multimem_coverage.md) |
| PTX 9.3 `prefetch` and `prefetchu` coverage | [EN](converage/prefetch_coverage.md) | [中文](../zh-han/converage/prefetch_coverage.md) |
| SELP Coverage | [EN](converage/selp_coverage.md) | [中文](../zh-han/converage/selp_coverage.md) |
| SET Coverage | [EN](converage/set_coverage.md) | [中文](../zh-han/converage/set_coverage.md) |
| SETP Coverage | [EN](converage/setp_coverage.md) | [中文](../zh-han/converage/setp_coverage.md) |
| SLCT Coverage | [EN](converage/slct_coverage.md) | [中文](../zh-han/converage/slct_coverage.md) |
| ST Coverage | [EN](converage/st_coverage.md) | [中文](../zh-han/converage/st_coverage.md) |
| Stack manipulation coverage | [EN](converage/stack_manipulation_coverage.md) | [中文](../zh-han/converage/stack_manipulation_coverage.md) |
| Surface instruction coverage | [EN](converage/surface_coverage.md) | [中文](../zh-han/converage/surface_coverage.md) |
| PTX Syntax Coverage | [EN](converage/syntax_coverage.md) | [中文](../zh-han/converage/syntax_coverage.md) |
| TCGEN descriptor field and layout coverage | [EN](converage/tcgen_descriptor_coverage.md) | [中文](../zh-han/converage/tcgen_descriptor_coverage.md) |
| Non-WS activation collector and ashift | [EN](converage/tcgen_mma_a_collector_coverage.md) | [中文](../zh-han/converage/tcgen_mma_a_collector_coverage.md) |
| TCGEN dense f16 MMA coverage | [EN](converage/tcgen_mma_coverage.md) | [中文](../zh-han/converage/tcgen_mma_coverage.md) |
| Dense unscaled f8f6f4 TCGEN MMA coverage | [EN](converage/tcgen_mma_f8f6f4_coverage.md) | [中文](../zh-han/converage/tcgen_mma_f8f6f4_coverage.md) |
| TCGEN MMA frontend family coverage | [EN](converage/tcgen_mma_family_coverage.md) | [中文](../zh-han/converage/tcgen_mma_family_coverage.md) |
| Dense i8 TCGEN MMA coverage | [EN](converage/tcgen_mma_i8_coverage.md) | [中文](../zh-han/converage/tcgen_mma_i8_coverage.md) |
| Dense MX4 block-scaled TCGEN MMA | [EN](converage/tcgen_mma_mx4_coverage.md) | [中文](../zh-han/converage/tcgen_mma_mx4_coverage.md) |
| Dense MX8 TCGEN MMA coverage | [EN](converage/tcgen_mma_mx8_coverage.md) | [中文](../zh-han/converage/tcgen_mma_mx8_coverage.md) |
| MX activation collector | [EN](converage/tcgen_mma_mx_a_collector_coverage.md) | [中文](../zh-han/converage/tcgen_mma_mx_a_collector_coverage.md) |
| Dense MX NV four-bit block-scaled TCGEN MMA | [EN](converage/tcgen_mma_mxnv_coverage.md) | [中文](../zh-han/converage/tcgen_mma_mxnv_coverage.md) |
| Ordinary sparse TCGEN MMA | [EN](converage/tcgen_mma_sparse_coverage.md) | [中文](../zh-han/converage/tcgen_mma_sparse_coverage.md) |
| Sparse block-scaled TCGEN MMA | [EN](converage/tcgen_mma_sparse_mx_coverage.md) | [中文](../zh-han/converage/tcgen_mma_sparse_mx_coverage.md) |
| TCGEN dense tf32 MMA coverage | [EN](converage/tcgen_mma_tf32_coverage.md) | [中文](../zh-han/converage/tcgen_mma_tf32_coverage.md) |
| Dense weight-stationary TCGEN MMA | [EN](converage/tcgen_mma_ws_dense_coverage.md) | [中文](../zh-han/converage/tcgen_mma_ws_dense_coverage.md) |
| Sparse weight-stationary TCGEN MMA | [EN](converage/tcgen_mma_ws_sparse_coverage.md) | [中文](../zh-han/converage/tcgen_mma_ws_sparse_coverage.md) |
| TCGEN specialized synchronization | [EN](converage/tcgen_sync_coverage.md) | [中文](../zh-han/converage/tcgen_sync_coverage.md) |
| Tensor-map and TMA frontend closeout matrix | [EN](converage/tensor_async_closeout.md) | [中文](../zh-han/converage/tensor_async_closeout.md) |
| Tiled tensor asynchronous data movement | [EN](converage/tensor_async_coverage.md) | [中文](../zh-han/converage/tensor_async_coverage.md) |
| Tensor Memory allocation coverage | [EN](converage/tensor_memory_allocation_coverage.md) | [中文](../zh-han/converage/tensor_memory_allocation_coverage.md) |
| Tensor Memory copy and shift | [EN](converage/tensor_memory_copy_shift_coverage.md) | [中文](../zh-han/converage/tensor_memory_copy_shift_coverage.md) |
| Tensor Memory register transfers | [EN](converage/tensor_memory_transfer_coverage.md) | [中文](../zh-han/converage/tensor_memory_transfer_coverage.md) |
| `testp` and `copysign` coverage | [EN](converage/testp_copysign_coverage.md) | [中文](../zh-han/converage/testp_copysign_coverage.md) |
| Texture and opaque resource coverage | [EN](converage/texture_coverage.md) | [中文](../zh-han/converage/texture_coverage.md) |
| Floating transcendental coverage | [EN](converage/transcendental_coverage.md) | [中文](../zh-han/converage/transcendental_coverage.md) |
| Floating reciprocal and square-root coverage | [EN](converage/unary_float_coverage.md) | [中文](../zh-han/converage/unary_float_coverage.md) |
| PTX 9.3 video instruction coverage | [EN](converage/video_coverage.md) | [中文](../zh-han/converage/video_coverage.md) |
| PTX 9.3 synchronized shuffle and vote coverage | [EN](converage/warp_sync_coverage.md) | [中文](../zh-han/converage/warp_sync_coverage.md) |
| PTX 9.3 WGMMA coverage | [EN](converage/wgmma_coverage.md) | [中文](../zh-han/converage/wgmma_coverage.md) |
