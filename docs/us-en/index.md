# Detailed reference index

Start with the [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki) for
technical guides and [current scope](coverage_status.md) for evidence boundaries.
These detailed source references retain design decisions, per-family contracts,
and reproducible evidence. Historical audit pins and build measurements describe
their recorded revisions, not current support. Matching English and Chinese
filenames are maintained together; code and tests decide documentation drift.

| Detailed topic | English | 中文 |
| --- | --- | --- |
| `abs` and `neg` floating-point coverage | [EN](abs_neg_coverage.md) | [中文](../zh-han/abs_neg_coverage.md) |
| Floating and mixed `add`/`sub` coverage | [EN](add_sub_coverage.md) | [中文](../zh-han/add_sub_coverage.md) |
| PTX 9.3 `applypriority` and `discard` coverage | [EN](applypriority_discard_coverage.md) | [中文](../zh-han/applypriority_discard_coverage.md) |
| PTX 9.3 atomic and reduction coverage | [EN](atomic_reduction_coverage.md) | [中文](../zh-han/atomic_reduction_coverage.md) |
| CTA `barrier.sync`, `barrier.arrive`, and `barrier.red` coverage | [EN](barrier_sync_coverage.md) | [中文](../zh-han/barrier_sync_coverage.md) |
| Integer Bit-Operation Coverage | [EN](bit_operations_coverage.md) | [中文](../zh-han/bit_operations_coverage.md) |
| Bitwise constant-expression signedness | [EN](bitwise_constant_policy.md) | [中文](../zh-han/bitwise_constant_policy.md) |
| Generated-model build scalability | [EN](build_scalability.md) | [中文](../zh-han/build_scalability.md) |
| Non-tensor bulk asynchronous data movement | [EN](bulk_async_coverage.md) | [中文](../zh-han/bulk_async_coverage.md) |
| Extended-precision integer coverage | [EN](carry_coverage.md) | [中文](../zh-han/carry_coverage.md) |
| Installed CMake Components | [EN](cmake_components.md) | [中文](../zh-han/cmake_components.md) |
| C++ naming and legacy-interface policy | [EN](code_conventions.md) | [中文](../zh-han/code_conventions.md) |
| Control-flow Operand Syntax Design | [EN](control_flow_syntax_design.md) | [中文](../zh-han/control_flow_syntax_design.md) |
| Conversion and Address-Query Coverage | [EN](conversion_coverage.md) | [中文](../zh-han/conversion_coverage.md) |
| Core Opcode Completeness Audit | [EN](core_opcode_completeness_audit.md) | [中文](../zh-han/core_opcode_completeness_audit.md) |
| Current coverage and evidence boundaries | [EN](coverage_status.md) | [中文](../zh-han/coverage_status.md) |
| PTX 9.3 non-bulk `cp.async` coverage | [EN](cp_async_coverage.md) | [中文](../zh-han/cp_async_coverage.md) |
| PTX 9.3 `createpolicy` coverage | [EN](createpolicy_coverage.md) | [中文](../zh-han/createpolicy_coverage.md) |
| Declaration Semantics Design | [EN](declaration_semantics_design.md) | [中文](../zh-han/declaration_semantics_design.md) |
| Floating `div` coverage | [EN](div_coverage.md) | [中文](../zh-han/div_coverage.md) |
| Fabric/CFT coverage | [EN](fabric_coverage.md) | [中文](../zh-han/fabric_coverage.md) |
| Ordinary `fence` coverage | [EN](fence_coverage.md) | [中文](../zh-han/fence_coverage.md) |
| FMA Coverage | [EN](fma_coverage.md) | [中文](../zh-han/fma_coverage.md) |
| Integer Arithmetic Coverage | [EN](integer_arithmetic_coverage.md) | [中文](../zh-han/integer_arithmetic_coverage.md) |
| LD Coverage | [EN](ld_coverage.md) | [中文](../zh-han/ld_coverage.md) |
| PTX 9.3 `ldu` coverage | [EN](ldu_coverage.md) | [中文](../zh-han/ldu_coverage.md) |
| PTX Lexer Design | [EN](lexer_design.md) | [中文](../zh-han/lexer_design.md) |
| Logic and Shift Coverage | [EN](logic_shift_coverage.md) | [中文](../zh-han/logic_shift_coverage.md) |
| Floating `mad` coverage | [EN](mad_coverage.md) | [中文](../zh-han/mad_coverage.md) |
| `mbarrier.test_wait` qualifier coverage | [EN](mbarrier_test_wait_coverage.md) | [中文](../zh-han/mbarrier_test_wait_coverage.md) |
| `mbarrier.try_wait` qualifier coverage | [EN](mbarrier_try_wait_coverage.md) | [中文](../zh-han/mbarrier_try_wait_coverage.md) |
| `membar` coverage | [EN](membar_coverage.md) | [中文](../zh-han/membar_coverage.md) |
| Floating `min`/`max` coverage | [EN](min_max_coverage.md) | [中文](../zh-han/min_max_coverage.md) |
| PTX 9.3 miscellaneous instruction coverage | [EN](miscellaneous_coverage.md) | [中文](../zh-han/miscellaneous_coverage.md) |
| MUL Coverage | [EN](mul_coverage.md) | [中文](../zh-han/mul_coverage.md) |
| Multimem frontend coverage (PTX ISA 9.3) | [EN](multimem_coverage.md) | [中文](../zh-han/multimem_coverage.md) |
| Parameter declarations | [EN](parameter_declarations.md) | [中文](../zh-han/parameter_declarations.md) |
| PTX 9.3 `prefetch` and `prefetchu` coverage | [EN](prefetch_coverage.md) | [中文](../zh-han/prefetch_coverage.md) |
| Python Generator Model Design | [EN](python_generator_model.md) | [中文](../zh-han/python_generator_model.md) |
| Register declaration compatibility | [EN](register_type_policy.md) | [中文](../zh-han/register_type_policy.md) |
| C++ Resolved IR Design | [EN](resolved_ir_design.md) | [中文](../zh-han/resolved_ir_design.md) |
| SELP Coverage | [EN](selp_coverage.md) | [中文](../zh-han/selp_coverage.md) |
| SET Coverage | [EN](set_coverage.md) | [中文](../zh-han/set_coverage.md) |
| SETP Coverage | [EN](setp_coverage.md) | [中文](../zh-han/setp_coverage.md) |
| SLCT Coverage | [EN](slct_coverage.md) | [中文](../zh-han/slct_coverage.md) |
| ST Coverage | [EN](st_coverage.md) | [中文](../zh-han/st_coverage.md) |
| Stack manipulation coverage | [EN](stack_manipulation_coverage.md) | [中文](../zh-han/stack_manipulation_coverage.md) |
| Resolved storage declarations | [EN](storage_declarations.md) | [中文](../zh-han/storage_declarations.md) |
| Surface instruction coverage | [EN](surface_coverage.md) | [中文](../zh-han/surface_coverage.md) |
| Symbol Binding Design | [EN](symbol_binding_design.md) | [中文](../zh-han/symbol_binding_design.md) |
| CST and Syntax AST design | [EN](syntax_ast_design.md) | [中文](../zh-han/syntax_ast_design.md) |
| PTX Syntax Coverage | [EN](syntax_coverage.md) | [中文](../zh-han/syntax_coverage.md) |
| TCGEN descriptor field and layout coverage | [EN](tcgen_descriptor_coverage.md) | [中文](../zh-han/tcgen_descriptor_coverage.md) |
| Non-WS activation collector and ashift | [EN](tcgen_mma_a_collector_coverage.md) | [中文](../zh-han/tcgen_mma_a_collector_coverage.md) |
| TCGEN dense f16 MMA coverage | [EN](tcgen_mma_coverage.md) | [中文](../zh-han/tcgen_mma_coverage.md) |
| Dense unscaled f8f6f4 TCGEN MMA coverage | [EN](tcgen_mma_f8f6f4_coverage.md) | [中文](../zh-han/tcgen_mma_f8f6f4_coverage.md) |
| TCGEN MMA frontend family coverage | [EN](tcgen_mma_family_coverage.md) | [中文](../zh-han/tcgen_mma_family_coverage.md) |
| Dense i8 TCGEN MMA coverage | [EN](tcgen_mma_i8_coverage.md) | [中文](../zh-han/tcgen_mma_i8_coverage.md) |
| Dense MX4 block-scaled TCGEN MMA | [EN](tcgen_mma_mx4_coverage.md) | [中文](../zh-han/tcgen_mma_mx4_coverage.md) |
| Dense MX8 TCGEN MMA coverage | [EN](tcgen_mma_mx8_coverage.md) | [中文](../zh-han/tcgen_mma_mx8_coverage.md) |
| MX activation collector | [EN](tcgen_mma_mx_a_collector_coverage.md) | [中文](../zh-han/tcgen_mma_mx_a_collector_coverage.md) |
| Dense MX NV four-bit block-scaled TCGEN MMA | [EN](tcgen_mma_mxnv_coverage.md) | [中文](../zh-han/tcgen_mma_mxnv_coverage.md) |
| Ordinary sparse TCGEN MMA | [EN](tcgen_mma_sparse_coverage.md) | [中文](../zh-han/tcgen_mma_sparse_coverage.md) |
| Sparse block-scaled TCGEN MMA | [EN](tcgen_mma_sparse_mx_coverage.md) | [中文](../zh-han/tcgen_mma_sparse_mx_coverage.md) |
| TCGEN dense tf32 MMA coverage | [EN](tcgen_mma_tf32_coverage.md) | [中文](../zh-han/tcgen_mma_tf32_coverage.md) |
| Dense weight-stationary TCGEN MMA | [EN](tcgen_mma_ws_dense_coverage.md) | [中文](../zh-han/tcgen_mma_ws_dense_coverage.md) |
| Sparse weight-stationary TCGEN MMA | [EN](tcgen_mma_ws_sparse_coverage.md) | [中文](../zh-han/tcgen_mma_ws_sparse_coverage.md) |
| TCGEN specialized synchronization | [EN](tcgen_sync_coverage.md) | [中文](../zh-han/tcgen_sync_coverage.md) |
| Tensor-map and TMA frontend closeout matrix | [EN](tensor_async_closeout.md) | [中文](../zh-han/tensor_async_closeout.md) |
| Tiled tensor asynchronous data movement | [EN](tensor_async_coverage.md) | [中文](../zh-han/tensor_async_coverage.md) |
| Tensor async cache controls | [EN](tensor_cache_controls.md) | [中文](../zh-han/tensor_cache_controls.md) |
| Caller-known tensor-map facts | [EN](tensor_map_known_facts.md) | [中文](../zh-han/tensor_map_known_facts.md) |
| Tensor Memory allocation coverage | [EN](tensor_memory_allocation_coverage.md) | [中文](../zh-han/tensor_memory_allocation_coverage.md) |
| Tensor Memory copy and shift | [EN](tensor_memory_copy_shift_coverage.md) | [中文](../zh-han/tensor_memory_copy_shift_coverage.md) |
| Tensor Memory register transfers | [EN](tensor_memory_transfer_coverage.md) | [中文](../zh-han/tensor_memory_transfer_coverage.md) |
| `testp` and `copysign` coverage | [EN](testp_copysign_coverage.md) | [中文](../zh-han/testp_copysign_coverage.md) |
| Texture and opaque resource coverage | [EN](texture_coverage.md) | [中文](../zh-han/texture_coverage.md) |
| Floating transcendental coverage | [EN](transcendental_coverage.md) | [中文](../zh-han/transcendental_coverage.md) |
| Floating reciprocal and square-root coverage | [EN](unary_float_coverage.md) | [中文](../zh-han/unary_float_coverage.md) |
| PTX 9.3 video instruction coverage | [EN](video_coverage.md) | [中文](../zh-han/video_coverage.md) |
| PTX 9.3 synchronized shuffle and vote coverage | [EN](warp_sync_coverage.md) | [中文](../zh-han/warp_sync_coverage.md) |
| PTX 9.3 WGMMA coverage | [EN](wgmma_coverage.md) | [中文](../zh-han/wgmma_coverage.md) |
| YAML Instruction Spec: Structure and Principles | [EN](yaml_instruction_spec.md) | [中文](../zh-han/yaml_instruction_spec.md) |
