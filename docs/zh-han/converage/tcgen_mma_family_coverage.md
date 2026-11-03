# TCGEN MMA 前端全族覆盖

固定 [CUDA 13.3 / PTX 9.3 的 TCGEN MMA 条款](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)定义四个源码家族。在当前目标目录内，下列 **22 个 `(家族, kind)` 身份**均有规范来源形式、直接类型化指令类、持有模块检查及条件性调用方已知值查询。这是固定形式的前端覆盖，不认证运行时描述符位或 GPU 执行。[规范来源数据](../../../python/src/ptx_frontend/spec/resources/ptx_spec/tensor_memory_data_movement.yaml)、[规范化器](../../../python/src/ptx_frontend/spec/normalize/tcgen_mma.py)、[运算规则行](../../../python/src/ptx_frontend/spec/tcgen_mma_operations.py)、[描述符字段](../../../python/src/ptx_frontend/spec/tcgen_descriptor_domains.py)及 [C++ 发射器](../../../python/src/ptx_frontend/code_gen/emit/tcgen_mma_operations.py)各自拥有相应规则；源码身份统计不从七个编码描述符 kind 推断。

| 固定源码家族 | Kind | 结构化操作数布局 | 专项契约 |
| --- | --- | ---: | --- |
| `tcgen05.mma` | f16、tf32、f8f6f4、i8、mxf8f6f4、mxf4、mxf4nvf4 | 18 | [f16](tcgen_mma_coverage.md)、[tf32](tcgen_mma_tf32_coverage.md)、[f8](tcgen_mma_f8f6f4_coverage.md)、[i8](tcgen_mma_i8_coverage.md)、[MX8](tcgen_mma_mx8_coverage.md)、[MX4](tcgen_mma_mx4_coverage.md)、[MX NV](tcgen_mma_mxnv_coverage.md) |
| `tcgen05.mma.sp` | 同样七种 kind | 18 | [普通稀疏](tcgen_mma_sparse_coverage.md)、[稀疏 MX](tcgen_mma_sparse_mx_coverage.md) |
| `tcgen05.mma.ws` | f16、tf32、f8f6f4、i8 | 16 | [稠密 WS](tcgen_mma_ws_dense_coverage.md) |
| `tcgen05.mma.ws.sp` | 同样四种 kind | 16 | [稀疏 WS](tcgen_mma_ws_sparse_coverage.md) |

当前 68 个布局统计操作数结构，不等于每一种 CTA group、selector、collector 动作或目标的笛卡尔积。
普通非 WS dense/sparse mask 必需：group 1 四个标量项，group 2 八个。
删除 24 个省略 mask 的布局保留全部 22 个家族/kind 身份及其他域。
此前统计的 92 个布局和 152 条拓扑元组是历史数据，包含现在已拒绝的省略形式。
生成身份与条件规则另有 [C++ 测试](../../../submod/resolved_ir/test/)和 [Python 测试](../../../python/tests/spec/)。

普通非 WS 形式保持各 kind 的 group、M/N/K、A 位置、掩码、D 缩放、转置与目标规则。[A collector 与 ashift](tcgen_mma_a_collector_coverage.md)扩展既有八种稠密／稀疏身份，不建立重叠类。六种稠密／稀疏 MX 身份共用两种 A 位置、带来源的类型化 selector、缩放 A/B 的 Tensor Memory 地址及 [A collector 控制](tcgen_mma_mx_a_collector_coverage.md)；不带通道掩码、D 缩放或 ashift。MX 缩放数据行明确保存 kind、稀疏状态、K、selector、角色、因子数、合法 ID 和放置策略。MX8 省略 selector 推导 1X；MX4 省略时推导 block32；MX NV 必须写出 selector。稀疏 MX 查询使用真实稀疏行及隐含的 F32 输出：表 46/47 不编码 D 类型字段。共享缩放行的生成投影保留稠密／稀疏身份，因此因子与 ID 检查能选中正确行。

两个 WS 家族都要求 CTA group 1，并暴露 B0–B3 collector；源码省略与显式 discard 保持区别。可选的最后零列操作数是标量 64 位寄存器；仅当该操作数出现时，才检查调用方独立提供的表 48 字。稀疏 WS 还要求元数据位于 B 之后、`idesc` 之前。共享路径行选择 M32/G、M64/E 或 M128/D，并要求适用的已知 A、D 及元数据通道半区为零。非 WS 的 F/C 半路径允许 0 或 16，并比较分别有效的已知半区；完整路径只允许零。即使 A 来自 shared，仍检查 D。MX4/MX NV 的 Python 查询先选择真实 K64/K96 行，再执行同一通道规则，不借用基础 f8 的 K32 路径。

[完成形式](tcgen_sync_coverage.md)向前端目录提供八种 `tcgen05.commit`、两种 fence 和两种 Tensor Memory wait。精确指令类、类型化完成身份、借用视图及[条件性 collector／历史报告](tcgen_mma_a_collector_coverage.md)向后续分析器提供源码和义务信息。前端不运行跨指令序列追踪器，也不证明先前 fill、fence、屏障或分配曾发生。普通来源接受、持有式引用／绑定检查、目标／版本检查与调用方已知字报告仍是不同层级的证据。

已知值报告可拒绝矛盾的描述符字段、形状／类型／目标组合、稀疏元数据索引、缩放事实和通道声明，但**不能**证明调用方字值等于运行时不透明的 `idesc`、共享描述符、零列字或 Tensor Memory 内容。固定表 47 的 MX4 K96 缩放 ID 域为 0/2，而后面的 K96 图也画出 1/3；前端执行已定义指令字的交集，记录冲突，不凭空扩大接受范围。M32 稀疏 WS 的元数据放置图尚未确立，因此报告保留 `MetadataLayoutRule`。固定来源未确定物理规则时，无缩放低位打包及部分低位转置仍是显式义务。实时稀疏内容、各 lane 的缩放放置、collector 历史、完成前来源稳定性、分配、同步和数值结果同样未获证明。

采用的 K96 encoding/layout 分离：Table 47 规定 A[29:30]/B[4:5] 的 code 域 {0,2}，图示物理 index 与字节单位 alignment guarantee 是不同的量。Block32 为三个 factor（Mx3/3xN），NV block16 为六个（Mx6/6xN），最低均为四字节。Layout-defined placement 下 ID 2 配 alignment 4/8 有效，不应用 `ID % alignment` 或推测 encoded-to-physical 转换。冲突标签 1/3 继续记录，缺失可选事实保留 obligation。[K96 解释](matrix_reference_interpretations.md#k96-encoded-scale-ids-and-physical-layout-facts)记录这一工程决策及不变的 opaque-word、live-content、unsupported-known-vector-layout 边界。

历史集成快照采用 Clang 21 Debug、Ninja 及配置好的 LLD 驱动路径验证。完整 CTest **1,357/1,357** 通过，其中 `TcgenMma*` 专项 **64/64**；Python CI 套件 **518/518** 通过。**13/13** 个已安装 MMA consumer 均完成配置、构建和运行；Python 3.14 wheel 构建、安装及资源 smoke 也通过。生成计划包含 **101 个条目、4,530 个形式、5,663 个操作数布局、913 个多布局形式和 390 个输出**（11 个全局输出，另有七类合计 379 个）。`GeneratorJobsTests.test_parallel_artifacts_match_serial_bytes_and_stable_manifest` 用合成发射器验证项目的并行写入／manifest 协议；`TcgenDescriptorGenerationTests.test_emitted_tables_are_deterministic_and_cover_literal_rules` 验证 TCGEN 描述符行。两者都不是对全部 390 个输出进行独立逐字节对比的声明。

集成序列中的首次 resolved-IR 构建耗时 **543.24 秒**、子进程峰值 RSS **4,146,040 KiB**；最终核心修复后的增量 resolved-IR 构建耗时 **197.11 秒**、峰值 **2,755,800 KiB**。两次缓存状态不同，不能当作干净构建性能对照。随后完整目标构建耗时 **11.93 秒**，完整 CTest 耗时 **106.40 秒**。固定 `ptxas` **13.3.73** 检查点的 **31/31 项预期结果**一致：19 组新增 kind、八项 collector／ashift／零列控制、两项精确 103a 稀疏 MX 和两项 selector 省略对照（30 项接受、一项预期拒绝）。最终构建未重跑该汇编语料；它也未覆盖全部目标或寄存器拼写，没有运行 GPU。[仓库中的源码测试](../../../submod/resolved_ir/test/test_tcgen_mma_sparse_mx.cpp)、[已知数据行测试](../../../python/tests/spec/test_tcgen_mma_sparse_mx_contract.py)与[安装后示例](../../../examples/tcgen_mma_ws_sparse_consumer/)可独立重跑前端正反例。

准备好仓库约定的 vcpkg 依赖及 Python 测试环境后，主要检查命令为：

```sh
cmake --preset ci-linux-clang-debug -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug --output-on-failure
.venv/bin/python -m unittest_parallel -s python/tests -t python -p 'test_*.py' --level=module -v
```

本地运行时，unittest-parallel 默认使用逻辑 CPU 数；CI 可以显式指定 worker 数。

有界汇编检查点使用完整 PTX 9.3 模块及 `/usr/local/cuda-13.3/bin/ptxas`，模块和日志包单独保留，未纳入本仓库。这里的 31/31 是已记录的工具观察；上面的命令与已链接测试则可直接从当前源码树复现。由此不能推断发布 CI、公开 ABI 冻结或模拟器／GPU 执行已获验证。

## 历史101 source profile 与验证 feature

`sm_101`/`compute_101` 及 A 形式从 PTX8.6 起支持，F 形式从 PTX8.8 起支持；PTX9.0/9.3 仍接受，rename 不引入上界。110 source profile 仍从 PTX9.0 起支持。`TargetIdentity` 保留 architecture101、flavor 和精确书写；`same_target_identity` 比较 source architecture/flavor，因此101A与110A仍不同。

新增 base 函数 `target_feature_architecture(identity)` 读取显式 private catalog 数据：三个101 key 投影到 feature architecture110，其他 key 保持原 architecture；未收录的 typed key 不会因此被识别。公共 `TargetProfile` 与 `TargetInfo` 布局不变。source/owned/semantic factory 用此 feature level 作为 numeric SM，同时保留 source identity，并传递既有110基础能力 `cluster`、`aggregate_smem`、`reserved_smem`、`graph_exec`。Generic101 没有 family；101A/F 仅启用 canonical `sm_110f`，不继承100f/103f，也不支持 FP64 mapping option。此有限对应基于公布的 rename 与 family 关系，不是物理硬件或 driver 保证。

availability 的 singular 与 span exact-target 要求通过该 helper 比较 validation-feature architecture 与精确 flavor；此关系不同于 source identity 和存储 key 相等。手动提供的 `TargetInfo::sm_version` 仍是 caller 的具体 feature 事实，checker 不会覆盖它。source profile 引入版本、instruction/PTX 门槛、capability 与 shape 条件仍独立。

历史指令数据仅修改已采用的231个 parent：219个保留100A clause并加入101A，仅原有110F分支时加入101F；12个 block-selector parent 追加8.8历史101A/F clause。854个原110条件及其版本门槛全部保留，DNF上限仍为六，25个受限 cluster gather4/im2col-W parent 不变。TCGEN 闭合 row 保留 kind、scaled-D 与显式 scale-vector 限制：历史 written block16/32 门槛为8.8，100-only vector 和 scaled-D 路径仍限定100。生成的 operational query 先验证精确 source profile 及其最低版本，再匹配 feature；未知 word 与地址仍为未决义务，不从实时 register 推断事实。

两个 module validator 均独立检查当前 function 的 `source_region`、`source_version`、精确 `source_target` 与当前 owned region 一致。合法 replacement AST 不能掩盖矛盾 cache。101/110 context 自身一致且当前 payload 在 supplied effective context 下合法时，允许通过 supplied header retarget；保留的 availability 不成为历史否决。缺少完整 instruction context 时继续既有 `AvailableContext` 推迟及严格 `MissingValidationContext` 政策。
