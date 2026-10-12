# 详细参考索引

技术指南从 [GitHub Wiki](https://github.com/endingly/ptx_frontend/wiki) 开始；
证据边界见[当前范围](converage/coverage_status.md)。详细源码参考保留设计决策、各族契约和
可复现证据。历史审计 pin 与构建测量描述其记录版本，不代表当前支持。
相同文件名的英中文档一同维护；代码与测试决定文档漂移。
设计、API 和策略参考保留在当前语言目录；覆盖度记录、审计与收尾矩阵
统一归入 `converage/` 子目录。

## 设计与参考

| 详细主题 | English | 中文 |
| --- | --- | --- |
| Bitwise constant-expression signedness | [EN](../us-en/bitwise_constant_policy.md) | [中文](bitwise_constant_policy.md) |
| Generated-model build scalability | [EN](../us-en/build_scalability.md) | [中文](build_scalability.md) |
| Installed CMake Components | [EN](../us-en/cmake_components.md) | [中文](cmake_components.md) |
| C++ naming and legacy-interface policy | [EN](../us-en/code_conventions.md) | [中文](code_conventions.md) |
| Control-flow Operand Syntax Design | [EN](../us-en/control_flow_syntax_design.md) | [中文](control_flow_syntax_design.md) |
| Declaration Semantics Design | [EN](../us-en/declaration_semantics_design.md) | [中文](declaration_semantics_design.md) |
| PTX Lexer Design | [EN](../us-en/lexer_design.md) | [中文](lexer_design.md) |
| Parameter declarations | [EN](../us-en/parameter_declarations.md) | [中文](parameter_declarations.md) |
| Python Generator Model Design | [EN](../us-en/python_generator_model.md) | [中文](python_generator_model.md) |
| Register declaration compatibility | [EN](../us-en/register_type_policy.md) | [中文](register_type_policy.md) |
| C++ Resolved IR Design | [EN](../us-en/resolved_ir_design.md) | [中文](resolved_ir_design.md) |
| Resolved storage declarations | [EN](../us-en/storage_declarations.md) | [中文](storage_declarations.md) |
| Symbol Binding Design | [EN](../us-en/symbol_binding_design.md) | [中文](symbol_binding_design.md) |
| CST and Syntax AST design | [EN](../us-en/syntax_ast_design.md) | [中文](syntax_ast_design.md) |
| Tensor async cache controls | [EN](../us-en/tensor_cache_controls.md) | [中文](tensor_cache_controls.md) |
| Caller-known tensor-map facts | [EN](../us-en/tensor_map_known_facts.md) | [中文](tensor_map_known_facts.md) |
| YAML Instruction Spec: Structure and Principles | [EN](../us-en/yaml_instruction_spec.md) | [中文](yaml_instruction_spec.md) |

## 覆盖度记录

| 详细主题 | English | 中文 |
| --- | --- | --- |
| `abs` and `neg` floating-point coverage | [EN](../us-en/converage/abs_neg_coverage.md) | [中文](converage/abs_neg_coverage.md) |
| Floating and mixed `add`/`sub` coverage | [EN](../us-en/converage/add_sub_coverage.md) | [中文](converage/add_sub_coverage.md) |
| PTX 9.3 `applypriority` and `discard` coverage | [EN](../us-en/converage/applypriority_discard_coverage.md) | [中文](converage/applypriority_discard_coverage.md) |
| PTX 9.3 atomic and reduction coverage | [EN](../us-en/converage/atomic_reduction_coverage.md) | [中文](converage/atomic_reduction_coverage.md) |
| CTA `barrier.sync`, `barrier.arrive`, and `barrier.red` coverage | [EN](../us-en/converage/barrier_sync_coverage.md) | [中文](converage/barrier_sync_coverage.md) |
| Integer Bit-Operation Coverage | [EN](../us-en/converage/bit_operations_coverage.md) | [中文](converage/bit_operations_coverage.md) |
| Non-tensor bulk asynchronous data movement | [EN](../us-en/converage/bulk_async_coverage.md) | [中文](converage/bulk_async_coverage.md) |
| Extended-precision integer coverage | [EN](../us-en/converage/carry_coverage.md) | [中文](converage/carry_coverage.md) |
| Conversion and Address-Query Coverage | [EN](../us-en/converage/conversion_coverage.md) | [中文](converage/conversion_coverage.md) |
| Core Opcode Completeness Audit | [EN](../us-en/converage/core_opcode_completeness_audit.md) | [中文](converage/core_opcode_completeness_audit.md) |
| Current coverage and evidence boundaries | [EN](../us-en/converage/coverage_status.md) | [中文](converage/coverage_status.md) |
| PTX 9.3 non-bulk `cp.async` coverage | [EN](../us-en/converage/cp_async_coverage.md) | [中文](converage/cp_async_coverage.md) |
| PTX 9.3 `createpolicy` coverage | [EN](../us-en/converage/createpolicy_coverage.md) | [中文](converage/createpolicy_coverage.md) |
| Floating `div` coverage | [EN](../us-en/converage/div_coverage.md) | [中文](converage/div_coverage.md) |
| Fabric/CFT coverage | [EN](../us-en/converage/fabric_coverage.md) | [中文](converage/fabric_coverage.md) |
| Ordinary `fence` coverage | [EN](../us-en/converage/fence_coverage.md) | [中文](converage/fence_coverage.md) |
| FMA Coverage | [EN](../us-en/converage/fma_coverage.md) | [中文](converage/fma_coverage.md) |
| Integer Arithmetic Coverage | [EN](../us-en/converage/integer_arithmetic_coverage.md) | [中文](converage/integer_arithmetic_coverage.md) |
| LD Coverage | [EN](../us-en/converage/ld_coverage.md) | [中文](converage/ld_coverage.md) |
| PTX 9.3 `ldu` coverage | [EN](../us-en/converage/ldu_coverage.md) | [中文](converage/ldu_coverage.md) |
| Logic and Shift Coverage | [EN](../us-en/converage/logic_shift_coverage.md) | [中文](converage/logic_shift_coverage.md) |
| Floating `mad` coverage | [EN](../us-en/converage/mad_coverage.md) | [中文](converage/mad_coverage.md) |
| `mbarrier.test_wait` qualifier coverage | [EN](../us-en/converage/mbarrier_test_wait_coverage.md) | [中文](converage/mbarrier_test_wait_coverage.md) |
| `mbarrier.try_wait` qualifier coverage | [EN](../us-en/converage/mbarrier_try_wait_coverage.md) | [中文](converage/mbarrier_try_wait_coverage.md) |
| `membar` coverage | [EN](../us-en/converage/membar_coverage.md) | [中文](converage/membar_coverage.md) |
| Floating `min`/`max` coverage | [EN](../us-en/converage/min_max_coverage.md) | [中文](converage/min_max_coverage.md) |
| PTX 9.3 miscellaneous instruction coverage | [EN](../us-en/converage/miscellaneous_coverage.md) | [中文](converage/miscellaneous_coverage.md) |
| MUL Coverage | [EN](../us-en/converage/mul_coverage.md) | [中文](converage/mul_coverage.md) |
| Multimem frontend coverage (PTX ISA 9.3) | [EN](../us-en/converage/multimem_coverage.md) | [中文](converage/multimem_coverage.md) |
| PTX 9.3 `prefetch` and `prefetchu` coverage | [EN](../us-en/converage/prefetch_coverage.md) | [中文](converage/prefetch_coverage.md) |
| SELP Coverage | [EN](../us-en/converage/selp_coverage.md) | [中文](converage/selp_coverage.md) |
| SET Coverage | [EN](../us-en/converage/set_coverage.md) | [中文](converage/set_coverage.md) |
| SETP Coverage | [EN](../us-en/converage/setp_coverage.md) | [中文](converage/setp_coverage.md) |
| SLCT Coverage | [EN](../us-en/converage/slct_coverage.md) | [中文](converage/slct_coverage.md) |
| ST Coverage | [EN](../us-en/converage/st_coverage.md) | [中文](converage/st_coverage.md) |
| Stack manipulation coverage | [EN](../us-en/converage/stack_manipulation_coverage.md) | [中文](converage/stack_manipulation_coverage.md) |
| Surface instruction coverage | [EN](../us-en/converage/surface_coverage.md) | [中文](converage/surface_coverage.md) |
| PTX Syntax Coverage | [EN](../us-en/converage/syntax_coverage.md) | [中文](converage/syntax_coverage.md) |
| TCGEN descriptor field and layout coverage | [EN](../us-en/converage/tcgen_descriptor_coverage.md) | [中文](converage/tcgen_descriptor_coverage.md) |
| Non-WS activation collector and ashift | [EN](../us-en/converage/tcgen_mma_a_collector_coverage.md) | [中文](converage/tcgen_mma_a_collector_coverage.md) |
| TCGEN dense f16 MMA coverage | [EN](../us-en/converage/tcgen_mma_coverage.md) | [中文](converage/tcgen_mma_coverage.md) |
| Dense unscaled f8f6f4 TCGEN MMA coverage | [EN](../us-en/converage/tcgen_mma_f8f6f4_coverage.md) | [中文](converage/tcgen_mma_f8f6f4_coverage.md) |
| TCGEN MMA frontend family coverage | [EN](../us-en/converage/tcgen_mma_family_coverage.md) | [中文](converage/tcgen_mma_family_coverage.md) |
| Dense i8 TCGEN MMA coverage | [EN](../us-en/converage/tcgen_mma_i8_coverage.md) | [中文](converage/tcgen_mma_i8_coverage.md) |
| Dense MX4 block-scaled TCGEN MMA | [EN](../us-en/converage/tcgen_mma_mx4_coverage.md) | [中文](converage/tcgen_mma_mx4_coverage.md) |
| Dense MX8 TCGEN MMA coverage | [EN](../us-en/converage/tcgen_mma_mx8_coverage.md) | [中文](converage/tcgen_mma_mx8_coverage.md) |
| MX activation collector | [EN](../us-en/converage/tcgen_mma_mx_a_collector_coverage.md) | [中文](converage/tcgen_mma_mx_a_collector_coverage.md) |
| Dense MX NV four-bit block-scaled TCGEN MMA | [EN](../us-en/converage/tcgen_mma_mxnv_coverage.md) | [中文](converage/tcgen_mma_mxnv_coverage.md) |
| Ordinary sparse TCGEN MMA | [EN](../us-en/converage/tcgen_mma_sparse_coverage.md) | [中文](converage/tcgen_mma_sparse_coverage.md) |
| Sparse block-scaled TCGEN MMA | [EN](../us-en/converage/tcgen_mma_sparse_mx_coverage.md) | [中文](converage/tcgen_mma_sparse_mx_coverage.md) |
| TCGEN dense tf32 MMA coverage | [EN](../us-en/converage/tcgen_mma_tf32_coverage.md) | [中文](converage/tcgen_mma_tf32_coverage.md) |
| Dense weight-stationary TCGEN MMA | [EN](../us-en/converage/tcgen_mma_ws_dense_coverage.md) | [中文](converage/tcgen_mma_ws_dense_coverage.md) |
| Sparse weight-stationary TCGEN MMA | [EN](../us-en/converage/tcgen_mma_ws_sparse_coverage.md) | [中文](converage/tcgen_mma_ws_sparse_coverage.md) |
| TCGEN specialized synchronization | [EN](../us-en/converage/tcgen_sync_coverage.md) | [中文](converage/tcgen_sync_coverage.md) |
| Tensor-map and TMA frontend closeout matrix | [EN](../us-en/converage/tensor_async_closeout.md) | [中文](converage/tensor_async_closeout.md) |
| Tiled tensor asynchronous data movement | [EN](../us-en/converage/tensor_async_coverage.md) | [中文](converage/tensor_async_coverage.md) |
| Tensor Memory allocation coverage | [EN](../us-en/converage/tensor_memory_allocation_coverage.md) | [中文](converage/tensor_memory_allocation_coverage.md) |
| Tensor Memory copy and shift | [EN](../us-en/converage/tensor_memory_copy_shift_coverage.md) | [中文](converage/tensor_memory_copy_shift_coverage.md) |
| Tensor Memory register transfers | [EN](../us-en/converage/tensor_memory_transfer_coverage.md) | [中文](converage/tensor_memory_transfer_coverage.md) |
| `testp` and `copysign` coverage | [EN](../us-en/converage/testp_copysign_coverage.md) | [中文](converage/testp_copysign_coverage.md) |
| Texture and opaque resource coverage | [EN](../us-en/converage/texture_coverage.md) | [中文](converage/texture_coverage.md) |
| Floating transcendental coverage | [EN](../us-en/converage/transcendental_coverage.md) | [中文](converage/transcendental_coverage.md) |
| Floating reciprocal and square-root coverage | [EN](../us-en/converage/unary_float_coverage.md) | [中文](converage/unary_float_coverage.md) |
| PTX 9.3 video instruction coverage | [EN](../us-en/converage/video_coverage.md) | [中文](converage/video_coverage.md) |
| PTX 9.3 synchronized shuffle and vote coverage | [EN](../us-en/converage/warp_sync_coverage.md) | [中文](converage/warp_sync_coverage.md) |
| PTX 9.3 WGMMA coverage | [EN](../us-en/converage/wgmma_coverage.md) | [中文](converage/wgmma_coverage.md) |
