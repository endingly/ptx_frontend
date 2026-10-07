# Tiled tensor 异步数据移动

Frontend 支持 [PTX ISA 9.3 §5.5 与 §9.7.9.26.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 中首批 tiled tensor-map 形式。复合操作数 `[tensorMap, {coords}]` 在 CST 与 AST 中保留嵌套地址、坐标元素、标点及源码范围。Resolved IR 自有 `ResolvedTensorMapRef` 和 rank 1–5 的 `ResolvedTensorOperand`。20 个精确 instruction class 都接受省略或显式 `.tile`；`tile` modifier 字段保留源码是否写出该限定符。

| 形式 | 操作数与完成机制 | 最低 PTX / 目标 |
| --- | --- | --- |
| `cp.async.bulk.prefetch.tensor.{1d…5d}.L2.global{.tile}` | `[tensorMap, {coords}]`；无 completion 操作数 | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cluster.global{.tile}.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {coords}], [mbar]`；mbarrier complete-tx-bytes | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cta.global{.tile}.mbarrier::complete_tx::bytes` | 相同操作数，CTA shared 目的地 | 8.6 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.global.shared::cta{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |

`tensorMap` 是指向 opaque 128 字节 descriptor 的 generic pointer。直接 descriptor 符号可位于 kernel `.param`、`.const` 或 `.global`，其声明身份、存储空间、对齐及源码范围保留在 owned IR。寄存器指针保留寄存器身份，但运行时来源与对齐未知。Tensor 数据方向单独建模：load 将 global tensor 数据写入 shared，store 从 CTA shared 读取并写入 global tensor 数据。

Checker 要求坐标数量恰好等于 rank，每个坐标具有 signed-32 语义，并接受兼容的 32-bit 寄存器。PTX 的 64-bit 整数常量在坐标使用处转换为 signed 32-bit，原始值及符号仍保留在 owned IR 中：`4294967296` 转换为零，`4294967295` 转换为 −1。Load 与 prefetch 可使用转换后为负的坐标；store 拒绝静态转换后为负的坐标，寄存器坐标值须在运行时检查。Owned-module 验证还会比较绑定坐标寄存器的缓存类型与其声明，包括 AST 已销毁的情况；无声明上下文的单指令检查仍延后处理缺失的 binding metadata。已知 descriptor、shared 数据、mbarrier 地址分别要求 64、16、8 字节对齐。Shared 数据检查采用 tiled mode 的基线；descriptor 不透明，依赖 swizzle mode 的更强对齐仍需运行时保证。未知寄存器指针对齐不视为已证明。已知 shared/local 或非 kernel parameter descriptor 会被拒绝，并检查 PTX/SM 可用性。

此切片无法静态证明 descriptor 内容、descriptor 内 rank 一致性、swizzle、stride、bounds、barrier locality 或运行时同步。`tensormap.replace`、`tensormap.cp_fenceproxy`、tensor reduction、im2col、gather/scatter、multicast、显式 CTA group、cache policy/hint 尚不在支持范围；相邻但未支持的拼写会在形式选择时失败。
