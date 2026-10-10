# TCGEN MMA 前端全族覆盖

固定 [CUDA 13.3 / PTX 9.3 的 TCGEN MMA 条款](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-mma)定义四个源码家族。在当前目标目录内，下列 **22 个 `(家族, kind)` 身份**均有规范来源形式、直接类型化指令类、持有模块检查及条件性调用方已知值查询。这是固定形式的前端覆盖，不认证运行时描述符位或 GPU 执行。[规范来源数据](../../python/src/ptx_frontend/spec/resources/ptx_spec/tensor_memory_data_movement.yaml)、[规范化器](../../python/src/ptx_frontend/spec/normalize/tcgen_mma.py)、[运算规则行](../../python/src/ptx_frontend/spec/tcgen_mma_operations.py)、[描述符字段](../../python/src/ptx_frontend/spec/tcgen_descriptor_domains.py)及 [C++ 发射器](../../python/src/ptx_frontend/code_gen/emit/tcgen_mma_operations.py)各自拥有相应规则；源码身份统计不从七个编码描述符 kind 推断。

| 固定源码家族 | Kind | 结构化操作数布局 | 专项契约 |
| --- | --- | ---: | --- |
| `tcgen05.mma` | f16、tf32、f8f6f4、i8、mxf8f6f4、mxf4、mxf4nvf4 | 30 | [f16](tcgen_mma_coverage.md)、[tf32](tcgen_mma_tf32_coverage.md)、[f8](tcgen_mma_f8f6f4_coverage.md)、[i8](tcgen_mma_i8_coverage.md)、[MX8](tcgen_mma_mx8_coverage.md)、[MX4](tcgen_mma_mx4_coverage.md)、[MX NV](tcgen_mma_mxnv_coverage.md) |
| `tcgen05.mma.sp` | 同样七种 kind | 30 | [普通稀疏](tcgen_mma_sparse_coverage.md)、[稀疏 MX](tcgen_mma_sparse_mx_coverage.md) |
| `tcgen05.mma.ws` | f16、tf32、f8f6f4、i8 | 16 | [稠密 WS](tcgen_mma_ws_dense_coverage.md) |
| `tcgen05.mma.ws.sp` | 同样四种 kind | 16 | [稀疏 WS](tcgen_mma_ws_sparse_coverage.md) |

92 个布局统计操作数结构，不等于每一种 CTA group、selector、collector 动作或目标的笛卡尔积。规范化来源统计将其展开为 152 条已记录的拓扑元组；相对保存的 40 条基线元组，新增 112 条、移除零条，40 个既有元组键也增加了可选限定符契约。统计核对了精确家族／kind 集合及 237 项来源断言，生成类身份与条件规则另有 [C++ 源码／已知值测试](../../submod/resolved_ir/test/)和 [Python 规则测试](../../python/tests/spec/)。这些数字只描述当前固定来源模型，不能推广至未来 PTX 版本的全部拼写。

普通非 WS 形式保持各 kind 的 group、M/N/K、A 位置、掩码、D 缩放、转置与目标规则。[A collector 与 ashift](tcgen_mma_a_collector_coverage.md)扩展既有八种稠密／稀疏身份，不建立重叠类。六种稠密／稀疏 MX 身份共用两种 A 位置、带来源的类型化 selector、缩放 A/B 的 Tensor Memory 地址及 [A collector 控制](tcgen_mma_mx_a_collector_coverage.md)；不带通道掩码、D 缩放或 ashift。MX 缩放数据行明确保存 kind、稀疏状态、K、selector、角色、因子数、合法 ID 和放置策略。MX8 省略 selector 推导 1X；MX4 省略时推导 block32；MX NV 必须写出 selector。稀疏 MX 查询使用真实稀疏行及隐含的 F32 输出：表 46/47 不编码 D 类型字段。共享缩放行的生成投影保留稠密／稀疏身份，因此因子与 ID 检查能选中正确行。

两个 WS 家族都要求 CTA group 1，并暴露 B0–B3 collector；源码省略与显式 discard 保持区别。可选的最后零列操作数是标量 64 位寄存器；仅当该操作数出现时，才检查调用方独立提供的表 48 字。稀疏 WS 还要求元数据位于 B 之后、`idesc` 之前。共享路径行选择 M32/G、M64/E 或 M128/D，并要求适用的已知 A、D 及元数据通道半区为零。非 WS 的 F/C 半路径允许 0 或 16，并比较分别有效的已知半区；完整路径只允许零。即使 A 来自 shared，仍检查 D。MX4/MX NV 的 Python 查询先选择真实 K64/K96 行，再执行同一通道规则，不借用基础 f8 的 K32 路径。

[完成形式](tcgen_sync_coverage.md)向前端目录提供八种 `tcgen05.commit`、两种 fence 和两种 Tensor Memory wait。精确指令类、类型化完成身份、借用视图及[条件性 collector／历史报告](tcgen_mma_a_collector_coverage.md)向后续分析器提供源码和义务信息。前端不运行跨指令序列追踪器，也不证明先前 fill、fence、屏障或分配曾发生。普通来源接受、持有式引用／绑定检查、目标／版本检查与调用方已知字报告仍是不同层级的证据。

已知值报告可拒绝矛盾的描述符字段、形状／类型／目标组合、稀疏元数据索引、缩放事实和通道声明，但**不能**证明调用方字值等于运行时不透明的 `idesc`、共享描述符、零列字或 Tensor Memory 内容。固定表 47 的 MX4 K96 缩放 ID 域为 0/2，而后面的 K96 图也画出 1/3；前端执行已定义指令字的交集，记录冲突，不凭空扩大接受范围。M32 稀疏 WS 的元数据放置图尚未确立，因此报告保留 `MetadataLayoutRule`。固定来源未确定物理规则时，无缩放低位打包及部分低位转置仍是显式义务。实时稀疏内容、各 lane 的缩放放置、collector 历史、完成前来源稳定性、分配、同步和数值结果同样未获证明。

最终来源快照采用 Clang 21 Debug、Ninja 及配置好的 LLD 驱动路径验证。完整 CTest **1,357/1,357** 通过，其中 `TcgenMma*` 专项 **64/64**；Python CI 套件 **518/518** 通过。**13/13** 个已安装 MMA consumer 均完成配置、构建和运行；Python 3.14 wheel 构建、安装及资源 smoke 也通过。生成计划包含 **101 个条目、4,530 个形式、5,663 个操作数布局、913 个多布局形式和 390 个输出**（11 个全局输出，另有七类合计 379 个）。`GeneratorJobsTests.test_parallel_artifacts_match_serial_bytes_and_stable_manifest` 用合成发射器验证项目的并行写入／manifest 协议；`TcgenDescriptorGenerationTests.test_emitted_tables_are_deterministic_and_cover_literal_rules` 验证 TCGEN 描述符行。两者都不是对全部 390 个输出进行独立逐字节对比的声明。

集成序列中的首次 resolved-IR 构建耗时 **543.24 秒**、子进程峰值 RSS **4,146,040 KiB**；最终核心修复后的增量 resolved-IR 构建耗时 **197.11 秒**、峰值 **2,755,800 KiB**。两次缓存状态不同，不能当作干净构建性能对照。随后完整目标构建耗时 **11.93 秒**，完整 CTest 耗时 **106.40 秒**。固定 `ptxas` **13.3.73** 检查点的 **31/31 项预期结果**一致：19 组新增 kind、八项 collector／ashift／零列控制、两项精确 103a 稀疏 MX 和两项 selector 省略对照（30 项接受、一项预期拒绝）。最终构建未重跑该汇编语料；它也未覆盖全部目标或寄存器拼写，没有运行 GPU。[仓库中的源码测试](../../submod/resolved_ir/test/test_tcgen_mma_sparse_mx.cpp)、[已知数据行测试](../../python/tests/spec/test_tcgen_mma_sparse_mx_contract.py)与[安装后示例](../../examples/tcgen_mma_ws_sparse_consumer/)可独立重跑前端正反例。

准备好仓库约定的 vcpkg 依赖及 Python 测试环境后，主要检查命令为：

```sh
cmake --preset ci-linux-clang-debug -DPython3_EXECUTABLE="$PWD/.venv/bin/python"
cmake --build --preset ci-linux-clang-debug
ctest --preset ci-linux-clang-debug --output-on-failure
.venv/bin/python -m unittest_parallel -s python/tests -t python -p 'test_*.py' --level=module -v
```

本地运行时，unittest-parallel 默认使用逻辑 CPU 数；CI 可以显式指定 worker 数。

有界汇编检查点使用完整 PTX 9.3 模块及 `/usr/local/cuda-13.3/bin/ptxas`，模块和日志包单独保留，未纳入本仓库。这里的 31/31 是已记录的工具观察；上面的命令与已链接测试则可直接从当前源码树复现。由此不能推断发布 CI、公开 ABI 冻结或模拟器／GPU 执行已获验证。
