# Tensor Memory 分配覆盖范围

前端建模 PTX 9.3 的三个 `tcgen05` 分配管理操作：`alloc`、`dealloc` 和
`relinquish_alloc_permit`。支持 `.cta_group::{1,2}` 两种取值。`alloc` 支持显式
`.shared::cta` 结果槽限定符和省略限定符的 generic 形式。结果是写入独立
shared-CTA 槽的四字节值，并非直接写入寄存器的分配 token。

```ptx
.version 8.6
.target sm_100a
.address_size 64
.entry kernel() {
  .shared .align 4 .b32 slot;
  .reg .b32 %taddr;
  tcgen05.alloc.cta_group::1.sync.aligned.shared::cta.b32 [slot], 32;
  ld.shared.b32 %taddr, [slot];
  tcgen05.dealloc.cta_group::1.sync.aligned.b32 %taddr, 32;
  tcgen05.relinquish_alloc_permit.cta_group::1.sync.aligned;
  ret;
}
```

| 契约 | 前端静态边界 |
| --- | --- |
| CTA group | 类型化的 `TcgenCtaGroup::One` 或 `Two`；同一 kernel/function body 内所有已支持的 TCGEN 操作必须一致。不同 body 可以选择不同 group。独立指令没有 enclosing-body 证明。 |
| `nCols` | scalar `.b32`/`.u32`/`.s32` general 寄存器，或转换为无符号 32 位的整数立即数。已知转换值只允许 32、64、128、256、512。动态值保留范围、二次幂和 group 内一致性义务。 |
| `taddr` | 独立的 owned `TensorMemoryAddress`，包装 scalar 32 位寄存器或转换后的立即数。前端不能从位模式推出有效分配。位 31:16 是 lane 地址，位 15:0 是列地址。 |
| `alloc` 结果 | 括号地址形式的独立 `ResolvedAddress` 结果槽角色；检查已知 shared-CTA 来源和四字节自然对齐。未解析的 generic 指针保留 shared-window 义务。已知不兼容空间和未对齐地址会失败。 |
| 操作元数据 | 每个生成 form 保留类型化的分配操作和许可效果。Group 值对应单个发起 warp，或两个对等 CTA 各一个 warp。这些是义务说明，不模拟运行时许可状态。 |

整数立即数在**使用时**收窄。例如，`4294967328` 与 `-4294967264` 都转换为合法的
列数 32。Owned 值保留原始整数源和转换后的位；语法树销毁后，checker 仍要求二者一致。

可用性采用精确限定目标：`sm_100a` 自 PTX 8.6 起，`sm_100f` family 自 PTX
8.8 起，`sm_110a` 和 `sm_110f` family 自 PTX 9.0 起。Generic `sm_100`、
`sm_120` family 和未知目标均不能代替这些 profile。

前端检查单条指令的类型、已知值、源码来源、目标门槛和每个 body 的 group 一致性。
前端不能证明对等 CTA 的参与、运行时分配大小变化、退出前释放、 relinquish 后的
许可状态或跨 device-function call 的一致性。Tensor Memory 寄存器 `ld`、
`st`、`ld.red` 和相应 wait 有[独立传输契约](tensor_memory_transfer_coverage.md)。
复制、位移及专用 commit/fence 操作仍未覆盖。此处不建模 GPU 执行或数值结果。

本地 Clang 21 Debug 构建编译了四个生成 form；C++ CTest 1151/1151 通过，
其中包含六个专门的分配测试。Packaged Python 规格和生成器测试 411/411
通过。全新安装的包 consumer 在释放语法树后成功配置、构建并验证 owned
module。`ptxas` 13.3.73 接受了 `sm_100a`/PTX 8.6、`sm_100f`/PTX 8.8、
`sm_110a`/`sm_110f`/PTX 9.0 的抽样分配语法；抽样的非法列数、carrier、
过早版本或 generic target 也被拒绝。汇编样本不证明 GPU 执行或运行时
collective 行为。
