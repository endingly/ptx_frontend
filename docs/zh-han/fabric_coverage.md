# Fabric/CFT 覆盖范围

前端支持固定 [PTX ISA 9.3（CUDA 13.3）](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#fabric-instructions)
中的六个设备端 Fabric Transport 源码族与六个 fabric proxy fence 形式，均要求
PTX 9.3 和 SM 100 及以上目标。单一 canonical `fabric` opcode 生成精确、自有的
指令类。方括号 CFT handle 是具名类型值：32 位 `endpoint`、64 位 `data_offset`，
counted 形式另有 64 位 `counter_offset`。每个分量保留寄存器绑定和源码位置。
当前支持相应宽度的整数/位寄存器；固定手册未定义 handle 分量的字面量域。

| 指令族 | 源码合同 | 完成与端点拓扑 |
| --- | --- | --- |
| `fabric.try_get` | `.async.shared::cta.relaxed.sys.b128`；`[dst]`、双分量源 handle、`size:u32`、`[bar]` | unicast；`mbarrier::complete_tx::bytes.mbarrier::report::fabric`；写 CTA shared |
| `fabric.try_put` | `.async[.multimem].shared::cta.relaxed.sys.b128`；双分量目标 handle、shared 源、大小、barrier；可选末尾 `.cp_mask` 的 `bytemask:u16`，或 counted 三分量目标 handle | unicast 或 multicast；`mbarrier::complete_tx::16B.mbarrier::report::fabric`；读 CTA shared |
| `fabric.try_red` | 与 put 相同方向和 handle 选择，另有 `and/or/xor`、`min/max`、`add` 及各自精确类型表 | unicast 或 multicast；`mbarrier::complete_tx::16B.mbarrier::report::fabric`；读 CTA shared |
| `fabric.try_pullred` | `.async.multimem.shared::cta.relaxed.sys.<op>.<type>.sync`；shared 目标、双分量源 handle、大小、barrier、立即数成员掩码 `0xffffffff` | multicast；`mbarrier::complete_tx::bytes.mbarrier::report::fabric`；写 CTA shared |
| `fabric.submit` | 零操作数；可选 `.op_restrict::fetching` | 提交本线程已发出的操作；fetching 仅提交 get/pullred |
| `fabric.wait` | `.sync_restrict::reads`，零操作数 | 已提交操作对 CTA shared 的读完成，非整个操作完成 |

`try_red` 的位运算支持 `b32/b64`；min/max 支持
`u32/s32/u64/s64/f16/bf16`；add 支持 `u32/u64/f16/bf16/f32/f64`。
`try_pullred` 的位运算相同，min/max 还支持 `e4m3/e5m2`，普通 add 支持
`u32/u64/f16/bf16/f32`；`.add.acc::f16` 支持 `e4m3/e5m2`，
`.add.acc::f32` 支持 `f16/bf16`。FP8 min/max 与累加 add 还需当前仓库的
SM 100f/110f 架构族能力或精确 SM 120a/121a 目标。counted put/red
不能与 `.cp_mask` 组合。
unicast `try_put` 文档中的 counted 前置写法
（`.async.counted::bytes.shared::cta`）解析成第二个精确源码顺序类，
其 CFT 合同与 canonical counted 写法相同。六族共有 43 个语义行、
44 个最终源码形式类。
手册中简写的 `try_red` 示例省略了必需的 `.relaxed.sys` 限定符；
前端以该族 Syntax 定义为准。

前端检查精确的后缀与操作数有无、宽度、handle 分量限寄存器、源码位置、
CTA shared 指针空间、可知的 shared 指针对齐、立即数大小为 16 的倍数，
以及 pullred 成员掩码的必需值。每个最终类的公开 `fabric_contract`
元数据记录操作、端点拓扑、shared 访问方向、完成类型、fabric 报告、
counted 模式和必需的 mbarrier layout v1。完成类型与状态报告不同；
状态检查复用现有 `mbarrier.try_wait.phase_type::primary` 报告形式，try 指令
没有状态结果操作数。操作成功时报告不变；失败时可设置谓词及不透明报告值。
get/pullred 的 `complete_tx::bytes` 计数为 `size` 字节；put/red 的
`complete_tx::16B` 计数为 `size / 16` 个事务。

运行时，数据指针和 CFT 数据 offset 必须 16 字节对齐，大小为 16 的倍数，
两个范围都须在界内。counted counter 是端点资源中的 8 字节对象，offset
须 256 字节对齐，且不能与目标数据重叠。当前 CTA 的 shared mbarrier
必须已用 layout v1 初始化。端点存在性、multicast 成员资格、counted 与
pull-reduction 能力、操作失败报告、warp 一致性、barrier phase 推进前
submit、grid 退出前最终完成、资源边界等都是运行时义务。同一 barrier 上
混用 fabric 与其他报告机制会产生未定义行为。静态 checker
不推断寄存器值或跨指令协议历史。

`fence.proxy.{generic::fabric,fabric::generic,fabric::fabric}.alias.{acquire,release}.sys`
没有操作数；有序 proxy pair 及 acquire/release 方向保留为类型化修饰符。
fence 负责 proxy 访问排序，不等待 Fabric 操作，也不报告状态。
主机端逻辑端点设置与 GPU 执行不在此前端覆盖范围内。

[Python 合同测试](../../python/tests/spec/test_fabric_contract.py)、
[C++ 覆盖测试](../../submod/resolved_ir/test/test_fabric_coverage.cpp)与
[安装后消费端](../../examples/fabric_consumer/main.cpp)在不执行 GPU 指令的情况下
检验源码及公开静态合同。
