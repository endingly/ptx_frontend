# Tiled tensor 异步数据移动

Frontend 支持 [PTX ISA 9.3 §5.5 与 §9.7.9.26.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 中选定的 tiled tensor-map 形式。复合操作数 `[tensorMap, {coords}]` 在 CST 与 AST 中保留嵌套地址、坐标元素、标点及源码范围。Resolved IR 自有 `ResolvedTensorMapRef` 和 rank 1–5 的 `ResolvedTensorOperand`。下表的基础 tiled 形式接受省略或显式 `.tile`；`tile` modifier 字段保留源码是否写出该限定符。

| 形式 | 操作数与完成机制 | 最低 PTX / 目标 |
| --- | --- | --- |
| `cp.async.bulk.prefetch.tensor.{1d…5d}.L2.global{.tile}` | `[tensorMap, {coords}]`；无 completion 操作数 | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cluster.global{.tile}.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {coords}], [mbar]`；mbarrier complete-tx-bytes | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cta.global{.tile}.mbarrier::complete_tx::bytes` | 相同操作数，CTA shared 目的地 | 8.6 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.global.shared::cta{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |

`tensorMap` 是指向 opaque 128 字节 descriptor 的 generic pointer。直接 descriptor 符号可位于 kernel `.param`、`.const` 或 `.global`，其声明身份、存储空间、对齐及源码范围保留在 owned IR。寄存器指针保留寄存器身份，但运行时来源与对齐未知。Tensor 数据方向单独建模：load 将 global tensor 数据写入 shared，store 从 CTA shared 读取并写入 global tensor 数据。

Checker 对普通 tiled 访问要求坐标数量恰好等于 rank，每个坐标具有 signed-32 语义，并接受兼容的 32-bit 寄存器。PTX 的 64-bit 整数常量在坐标使用处转换为 signed 32-bit，原始值及符号仍保留在 owned IR 中：`4294967296` 转换为零，`4294967295` 转换为 −1。Load 与 prefetch 可使用转换后为负的坐标；store 拒绝静态转换后为负的坐标，寄存器坐标值须在运行时检查。Owned-module 验证还会比较绑定坐标寄存器的缓存类型与其声明，包括 AST 已销毁的情况；无声明上下文的单指令检查仍延后处理缺失的 binding metadata。已知 descriptor、shared 数据、mbarrier 地址分别要求 64、16、8 字节对齐。Shared 数据检查采用 tiled mode 的基线；descriptor 不透明，依赖 swizzle mode 的更强对齐仍需运行时保证。未知寄存器指针对齐不视为已证明。已知 shared/local 或非 kernel parameter descriptor 会被拒绝，并检查 PTX/SM 可用性。

已建模的 TMA 家族还包括选定的 `tensormap.replace` 与 `tensormap.cp_fenceproxy` 形式、tensor reduction、rank 3–5 的 `im2col`/`im2col::w`/`im2col::w::128`、rank-2 四行 gather/scatter、cluster multicast mask，以及显式 `.cta_group::1/::2` 路由。具体 mode、方向、操作数与 target 组合由生成的 descriptor 选择；相邻但未支持的组合会在选择或检查时失败。同一函数内的 TMA 可混用省略、group-1 与 group-2 拼写，因为 TMA group 路由按指令确定。[TCGEN group 规则](tensor_memory_allocation_coverage.md)只作用于 TCGEN 指令。

[`TensorMapKnownFacts` 与选定用途投影](tensor_map_known_facts.md)可针对强类型选定指令，有条件地检查调用方提供的 descriptor 事实。`project_tensor_known_access_context(const Instruction&, const checker::Context&)` 返回复制的 access 和诊断：非 tensor 形式二者均无；tensor metadata 损坏时有诊断而无 access；target 不可用时保留复制的 access，并把 availability 标为 false。该查询不解码或认证原始 128 字节 descriptor。若没有调用方事实或运行时执行，descriptor 内容、与真实字节的 rank 一致性、swizzle、stride、bounds、barrier locality 和同步仍未得到证明。已支持的 tensor load、store、reduction 与 prefetch 形式现有规范 `.L2::cache_hint` 及可选的末尾 B64 cache-policy 操作数；详见 [tensor 缓存控制](tensor_cache_controls.md)。固定手册未给出完整 128 字节字段布局，故 raw-object decoder 不在此前端切片范围内；known-facts 与 cache 查询都不解码 descriptor 字节。[frontend 收口矩阵](tensor_async_closeout.md)逐项链接路线图要求、源码和测试。
