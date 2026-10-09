# Tensor-map 与 TMA frontend 收口矩阵

此矩阵以固定的 [CUDA 13.3 PTX ISA 9.3][ptx] 审计 M14-I01～I22 和
M14-C01～C03，并对应当前源码。“覆盖”指已支持的源码形式可无损保留拼写和位置，
降为自有强类型 IR，按指令操作数和选中 PTX/target profile 做局部检查。
Tensor map 仍是**不透明的 128 字节对象**；选中指令和调用方提供的
[`TensorMapKnownFacts`][facts-doc] 都不认证其原始字节。固定 PTX 9.3 的规范
tensor load、store、reduction 与 prefetch 源码模板，在当前 target catalog 中
对应 356 个不同的选定身份：load 180、store 18、reduction 128、prefetch 30。
其中有 178 对 base/cache-hint 形式，非缓存操作数与可选 policy layout 一致。
此清单只涉及文档化的源码模板与 catalog target，不证明 GPU 执行、运行时同步、
原始 descriptor 内容或未文档化别名。

#152 的主要切片是 tensor-map 和 tensor-copy 形式。非 tensor bulk 形式及其完成
指令是 #151 已交付的共享依赖；`red.async` 属于原子/归约家族。路线图将它们列在
M14，故一并列证，但不改变归属。准确支持的形式及相邻拒绝形式以所链 schema
和测试为准；单行覆盖不表示允许未列出的后缀、mode 或 target 组合。

| 路线图 | PTX 9.3 要求 | 源码契约 | 可执行证据 | 归属 |
| --- | --- | --- | --- | --- |
| M14-I01 | [§5.5.1 dimension/format][dimension]、[§5.5.2 access mode][modes]、[§5.5.8 tensor map][map] | [rank/mode 与 Table 33 强类型域][ir]；[有条件的 element/swizzle/interleave 事实][facts] | [replacement 字段][replace-test]、[known-facts 规则][facts-test] | #152，descriptor 值有条件 |
| M14-I02 | [§5.5.8 map 操作数][map] | [自有 `ResolvedTensorMapRef` 地址、身份和范围][ir]；[选中形式][schema] | [参数/符号/寄存器 descriptor][tensor-test] | #152 |
| M14-I03 | [§9.7.9.26.5.2 坐标][tensor-copy] | [自有坐标及源码范围][ir]；[rank/mode 绑定][model] | [CST/AST 复合操作数][ast-test]、[rank/符号转换][tensor-test] | #152 |
| M14-I04 | [§9.7.9.27 `tensormap.replace`][replace] | [replacement 字段/编码域][ir]、[type/target checker][checker] | [字段、值、target 与篡改测试][replace-test] | #152 |
| M14-I05 | [§9.7.14.17 `tensormap.cp_fenceproxy`][proxy] | [proxy 形式][schema]、[scope/size checker][checker] | [proxy scope、操作数与篡改测试][replace-test] | #152 |
| M14-I06 | [§9.7.9.26.4.1 global→cluster 复制][bulk-copy] | [非 tensor bulk 形式][schema]、[地址/完成机制 checker][checker] | [复制与 group topology][bulk-test] | #151 共享 |
| M14-I07 | [§9.7.9.26.4.1 CTA→global 复制][bulk-copy] | [方向明确的 bulk 形式][schema]、[地址/完成机制 checker][checker] | [复制与 group topology][bulk-test] | #151 共享 |
| M14-I08 | [§9.7.9.26.4.1 `.sem/.scope`][bulk-copy] | [精确限定符与可用性][schema] | [限定符矩阵及非法组合][bulk-test] | #151 共享 |
| M14-I09 | [§9.7.9.26.4.2 bulk 归约][bulk-reduce] | [操作/类型及完成机制形式][schema] | [归约/scope 与负例][bulk-test] | #151 共享 |
| M14-I10 | [§9.7.9.26.4.3 bulk prefetch][bulk-prefetch] | [L2/global prefetch 形式][schema] | [归约与 prefetch 测试][bulk-test] | #151 共享 |
| M14-I11 | [§9.7.9.26.5.2 tiled load][tensor-copy] | [rank 1–5 shared 目的地和 mbarrier 形式][schema]；[强类型 mode][ir] | [全部 rank、方向和指针检查][tensor-test] | #152 |
| M14-I12 | [§9.7.9.26.5.2 tiled store][tensor-copy] | [rank 1–5 global 目的地和 bulk-group 形式][schema] | [store rank 与带符号坐标][tensor-test] | #152 |
| M14-I13 | [§9.7.9.26.5.3 tensor 归约][tensor-reduce] | [选定 op/rank/mode 身份][schema]；[强类型归约 metadata][model] | [身份、element type 和 target][reduction-test] | #152 |
| M14-I14 | [§9.7.9.26.5.4 tensor prefetch][tensor-prefetch] | [rank 1–5 形式][schema]、[自有 tensor 操作数][ir] | [prefetch rank 与 tile 来源][tensor-test] | #152 |
| M14-I15 | [§9.7.9.26.5.2 im2col][tensor-copy] | [im2col 与 no-offset mode、U16 info 角色][model] | [info 元数/类型][im2col-test]、[no-offset 形式][nooffset-test] | #152 |
| M14-I16 | [§9.7.9.26.5.2 W/W128][tensor-copy] | [W halo/offset 与精确 target 可用性][model] | [W 家族 target 边界及负例][im2col-test] | #152 |
| M14-I17 | [§9.7.9.26.5.2 gather/scatter][tensor-copy] | [四行角色和 rank-two mode][ir]；[形式][schema] | [四种自有形式及相邻拒绝][gather-test] | #152 |
| M14-I18 | [§9.7.9.26.6.1 bulk commit][commit] | [独立 `bulk_group` 完成类型][completion]；[形式][schema] | [复制/group topology][bulk-test] | #151 共享 |
| M14-I19 | [§9.7.9.26.6.2 bulk wait][wait] | [常量计数与可选 `.read`][schema] | [复制/group topology 与非法形式][bulk-test] | #151 共享 |
| M14-I20 | [§9.7.9.12 `st.async`][store-async] | [shared mbarrier 与 global release 形式][schema] | [store topology 与目的地基址][bulk-test] | #151 共享 |
| M14-I21 | [§9.7.9.14 `st.bulk`][store-bulk] | [zero-fill size/type/target 形式][schema] | [store size/version 与自有绑定][bulk-test] | #151 共享 |
| M14-I22 | [§9.7.14.7 `red.async`][red-async] | [shared completion 和 global release 形式][red-schema] | [async 归约 mode 与负例][atomic-test] | 原子/归约共享 |
| M14-C01 | [§9.7.9.26.6 完成机制][completion-ptx] | [统一的 `AsyncCompletionKind` 域][completion]区分 async-group、bulk-group 和 mbarrier complete-tx-bytes；指令选定具体值 | [bulk topology][bulk-test]、[tensor 方向][tensor-test] | #151/#152 共享 |
| M14-C02 | [§9.7.9.26.5.1 限制][restrictions] | [rank/mode/坐标/方向/空间 checker][checker]；[有条件的 descriptor 事实查询][facts] | [tensor][tensor-test]、[im2col][im2col-test]、[known facts][facts-test] | #152，不透明边界 |
| M14-C03 | [§9.7.9.26.4～.6 形式][tensor-copy] | [schema 变体与精确 target 可用性][schema] | [sm90a/sm100 正例和相邻负例][tensor-test]、[bulk][bulk-test]、[mode][im2col-test] | #151/#152 共享语料 |

以下跨项形式同样属于 #152 的已支持前端切片：cluster multicast mask 在
[schema][schema]、[typed mask][ir] 与[正反例][multicast-test]中覆盖；
`.cta_group::1/::2` 的逐指令路由在 [typed role][ir] 和[混合组/target 测试][group-test]中
覆盖；tiled/reduction/store 的 `.im2col_no_offs` 在[模式测试][nooffset-test]中覆盖；
规范 `.L2::cache_hint` 及可选末尾 B64 policy 在 [schema][schema]、
[自有查询][cache]和[正反例/篡改测试][cache-test]中覆盖。
这些行的精确 PTX 条款分别为[§9.7.9.26.5.2][tensor-copy]、
[§9.7.9.26.5.3][tensor-reduce]和[§9.7.9.26.5.4][tensor-prefetch]。

独立的 [generic `prefetch.tensormap` 形式](prefetch_coverage.md)允许已知 shared 地址，
其效果按规范为无操作；显式 `.shared.tensormap` 拼写仍不在选定形式中。脱离 AST 的
module validation 会将每个已绑定地址符号缓存的类型和保证对齐与 owned declaration
比对，包括嵌在 tensor operand 中的 tensor-map 引用。[prefetch 绑定回归][prefetch-test]
和[嵌套 tensor 回归][tensor-test]在 AST 销毁后验证这两项边界。

上述源码回归覆盖选定形式与相邻负例，并不证明每一种可能的指令组合都能汇编。
[PTXAS 13.3.73 的 26 个留存探针][ptxas]是有界的 cache-control 实验，
与 C++ 语料分开。它们展示正式 PTX 语法与该汇编器对 hint-only 形式的分歧，
不证明运行时 cache 效果。

静态检查可建立已知地址空间/对齐、操作数形状、字面量转换和选中形式的精确
target 可用性。[22 个调用方已知事实规则族][facts-doc]可有条件地检查提供的
descriptor 断言，包括部分 bounds 和由 swizzle 决定的对齐关系；缺失事实保持
`Unresolved`。若没有足够的调用方事实或运行时值，原始 descriptor 字段、坐标、
peer/mask 成员资格、barrier locality/completion、bounds、由 swizzle 决定的对齐及
真实内存效果仍未得到证明。完整的 128 字节原始对象解码需要另行明确字节布局，
不在本次收口范围内。

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
[prefetch-test]: ../../submod/resolved_ir/test/test_prefetch_completeness.cpp
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
