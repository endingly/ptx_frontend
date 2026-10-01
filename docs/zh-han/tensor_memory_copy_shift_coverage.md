# Tensor Memory 复制与位移

前端在现有 `tcgen05` 根指令下建模 PTX 9.3 的源代码可见 `tcgen05.cp` 和 `tcgen05.shift`。一个复制存储形式表示 **36** 个合法组合：六种形状/组播配对、两个书写的 CTA group 和三种成对格式。一个位移存储形式表示两个 group 标识，每个 group 接受文档中的两种修饰符顺序。物理拥有形式数量与语义组合数量不同。

| 复制形状 | 书写的组播修饰符 |
| --- | --- |
| `.128x256b`、`.4x256b`、`.128x128b` | 省略 |
| `.64x128b` | `.warpx2::02_13` 或 `.warpx2::01_23` |
| `.32x128b` | `.warpx4` |

格式配对只能是省略、`.b8x16.b6x16_p32` 或 `.b8x16.b4x16_p64`。目标与源格式 token 在拥有形式中分别保留源位置。缺少一半的格式、两个源格式或不合法的形状/组播配对都会被拒绝。复制恰有 `[taddr], s-desc` 两个操作数；位移恰有 `[taddr]` 一个操作数。位移的 `.31x256b` 是隐含移动形状，不是书写限定符。`tcgen05.shift.cta_group::1.down` 和 `tcgen05.shift.down.cta_group::1` 均归于同一个 group-1 标识；group 2 同理。

`[taddr]` 复用带 U32 使用处转换及原始 64 位整数源值的 Tensor Memory 方括号地址。它接受标量 General 32 位寄存器或转换后的整数立即数，不接受普通指针偏移。位移要求已知转换后地址的第 31:16 位 lane 分量可被 32 整除。前端对已知立即数检查这一点；寄存器值的 lane 对齐仍是运行时义务。这里不臆造字节地址或列限制。

`s-desc` 是兼容 B64/U64/S64 的标量 General 寄存器。复制形式对拥有的寄存器提供借用的、独立且不透明的 TCGEN Table 43 描述符角色视图，并在 AST 销毁后检查已知声明及模块绑定。视图生命周期随拥有指令载荷结束。寄存器拼写无法揭示编码描述符内容：固定/保留位、共享内存基址、leading stride、swizzle、padding、分配及布局有效性仍为运行时义务。此角色不同于 WGMMA 共享描述符和 TensorMap。[TCGEN 描述符字段与布局 API](tcgen_descriptor_coverage.md) 独立查询调用方提供的已知 word；它不解码或认证此借用寄存器的运行时内容。

复制从 PTX 8.6 的精确 `sm_100a`、8.8 的 `sm_100f` 家族或 9.0 的精确 `sm_110a` / `sm_110f` 家族起可用。位移只接受 8.6 的精确 `sm_100a`、8.8 的精确 `sm_103a` 或 9.0 的精确 `sm_110a`。通用、无关家族及 `sm_120` 目标不能替代。group 限定符与分配、传输和 commit 一起参与每个函数体的 group 一致性检查；不同函数体可以选择不同 group。两个操作都可以是匹配 `tcgen05.commit` mbarrier-arrive-one 协议的先前同线程异步 TCGEN 工作。它们本身不触发 barrier 信号，也不证明完成。专用 fence 和 `fence.proxy.async` 保留各自的排序及可见性义务。

官方 13.3.73 汇编证据接受全部 36 个复制组合、两个 group 的两种位移顺序、兼容的 U64/S64 描述符载体及有效目标端点。部分格式配对及经匹配对照的无效载体和目标被拒绝。PTX 8.5 的 `sm_100a` 仅指令目标对照失败，因此两个低于引入版本的候选被跳过，不能据此宣称独立的指令版本下限证据。汇编不能证明运行时描述符布局、解压行为、数据移动、对等 CTA 参与或完成；未执行 GPU 测试。

来源：[固定 CUDA 13.3 / PTX 9.3 复制条款](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-cp)、[位移条款](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#tensorcore-5th-generation-instructions-tcgen05-shift)及[共享内存描述符 Table 43](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#shared-memory-descriptor)。
