# Matrix 参考解释

本文记录 frontend 对固定版本 [PTX ISA 9.3 参考文档](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)
中局部矛盾的有界解释。这些是项目工程解释，并非 NVIDIA 发布的勘误。每项解释只覆盖
自己的范围，不代表整个 matrix 指令族覆盖完成或硬件执行符合性。
[English](../../us-en/converage/matrix_reference_interpretations.md)

## WMMA `m8n8k4` F64 fragments

此 shape 的 `wmma.load`、`wmma.mma`、`wmma.store` 中，每个线程的 A、B 各使用
一个 64 位寄存器，C、D 各使用两个 64 位寄存器。
[fragment 表格](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment)
中 F64 C/D 的单寄存器条目与其他条款矛盾。本项目采用两个寄存器：
[shape 定义](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
使 C/D 成为含 64 个 F64 元素的 8×8 矩阵，在 32 个线程中每线程需要两个元素。
A 为 8×4，B 为 4×8，每线程各需一个元素。
[store 示例](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-st)
与 [MMA 示例](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-wmma-mma)
均使用两个 C/D 寄存器，与容量推导一致。此解释只解决这一局部矛盾，不建立“示例普遍
优先于表格”的规则。

```ptx
wmma.load.c.sync.aligned.m8n8k4.col.global.f64 {c0,c1}, [p], 8;
wmma.mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
wmma.store.d.sync.aligned.m8n8k4.row.global.f64 [p], {d0,d1}, 8;
```

C/D fragment 占 16 字节，内存地址要求 16 字节对齐。Leading dimension 仍是依赖
layout 的元素步长，最小值与整除条件独立检查。row/col 布局、现有内存空间、默认
`.rn` 与显式 `.rn/.rz/.rm/.rp` 舍入方式均保留。基础门槛为 PTX 7.0 / `sm_80`，
`shared::cta` 保留独立的 PTX 7.8 门槛。普通标量 F64 与兼容 B64 data carrier
沿用现有规则。元素在 lane/register 间的物理分布仍依赖具体架构。

Canonical 规则已表达此解释。
[`test_wmma_coverage.cpp`](../../../submod/resolved_ir/test/test_wmma_coverage.cpp)
与 [`test_wmma_coverage.py`](../../../python/tests/spec/test_wmma_coverage.py) 中的定向
回归分别检查精确的 36 个静态 identity 与实际 source 程序，拒绝 A/B 双寄存器及
C/D 单/三寄存器，在 syntax owner 销毁后独立修改 owned fragment 并检查。
Parser 接受 brace pack 本身不代表语义接受。Owned copy/clone、外部 source 验证、
对齐、标量宽度、target 和 stride 保持为独立检查。

## 标准 F64 MMA shape 拼写

Dense F64 接受的 shape 为 `.m8n8k4`。F64
[Syntax 小节](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-mma)
中的 `.m8n84` 被视为编辑笔误，不是兼容别名。
[shape 表格](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)、
独立的 [F64 fragment 小节](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-fragment-mma-884-f64)、
ISA 注记与示例均使用完整的 M/N/K shape `m8n8k4`。本项目保留这一完整拼写，不增加
source 重写或 lexer 特例。这是局部工程解释，不是厂商发布的勘误。

```ptx
mma.sync.aligned.m8n8k4.row.col.f64.f64.f64.f64 {d0,d1}, {a}, {b}, {c0,c1};
```

此形式的 `MatrixShape{8,8,4}`、A/B 的 row/col layout、F64 标量类型和 D/A/B/C
寄存器数量 2/1/1/2 保持不变。最低要求为 PTX 7.0 / `sm_80`，默认 `.rn`，支持
显式 `.rn/.rz/.rm/.rp` 舍入。独立的 F64 `m16n8k4`、`m16n8k8`、`m16n8k16`
形式保留 PTX 7.8 / `sm_90` 门槛。这些是邻接控制，不是本 shape 的别名。

Canonical 规则已实现此解释。
[`test_warp_matrix_mma_coverage.cpp`](../../../submod/resolved_ir/test/test_warp_matrix_mma_coverage.cpp)
与 [`test_warp_matrix_mma_coverage.py`](../../../python/tests/spec/test_warp_matrix_mma_coverage.py)
中的定向测试关联精确的 raw/normalized/IR identity，并区分通用 CST/AST 接受与语义
resolve：`.m8n84`、`.m8n8`、`.m8n8k`、`.m8n8k5` 可以被解析，但在 resolution 阶段
因未知 modifier 被拒绝。不存在 owned 笔误 variant；其缺失通过 source selection 验证，
不修改生成的 immutable shape/suffix 元数据。其他测试独立修改实际 owned fragment、
layout tag 与绑定一致的真实 F32 引用，检查诊断范围、source 关联、AST 销毁后的
copy/clone 以及独立的 target 门槛。这些检查不认证运行时寄存器内容或物理 fragment 分布。

## Ordered sparse low-bit kind 分隔符

`mma.sp::ordered_metadata.sync.aligned.m16n8k64` 家族接受的 low-bit suffix 为
`.kind::f8f6f4`，包含开头的点。
[sparse MMA 小节](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-instructions-sparse-mma)
中 kind-domain 定义 `{kind::f8f6f4}` 遗漏的点被解释为元语言排版损坏，不是另一种 source 拼写。
Suffix 分隔语法与该小节完整示例均使用 `.kind::f8f6f4`；
[shape 表格](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#warp-level-matrix-shape)
标明对应的 ordered low-bit shape/type 家族。这是项目工程解释，不是厂商发布的勘误，
也不引入新的 modifier 顺序限制。

```ptx
mma.sp::ordered_metadata.sync.aligned.m16n8k64.row.col.kind::f8f6f4.f32.e3m2.e2m3.f32
  {d0,d1,d2,d3}, {a0,a1,a2,a3}, {b0,b1,b2,b3}, {c0,c1,c2,c3}, meta, 0;
```

现有 catalogue 包含 50 个 identity：A/B 类型各自从 `e4m3`、`e5m2`、`e3m2`、
`e2m3`、`e2m1` 中独立选择，D/C 必须同为 F16/F16 或 F32/F32。A/B 各使用四个
B32 container。F16 D/C 各使用两个 F16x2 寄存器，F32 D/C 各使用四个 F32 寄存器。
Ordered metadata 使用 B32 寄存器与 selector 0。既有门槛保持为 PTX 8.7 / `sm_120a`
或 PTX 8.8 / qualified `sm_120f` family；generic `sm_120` profile 不提供该资格。

Canonical 规则已表达此解释。
[`test_warp_matrix_mma_coverage.cpp`](../../../submod/resolved_ir/test/test_warp_matrix_mma_coverage.cpp)
与 [`test_warp_matrix_mma_coverage.py`](../../../python/tests/spec/test_warp_matrix_mma_coverage.py)
中的定向测试分别检查 50 个静态 identity 和五组 source 代表。缺少分隔符或未知 kind
在 resolution 阶段作为未知 modifier 被拒绝。重复点产生 parser 诊断，即使恢复过程
返回 CST/AST 值也不代表接受。Native `.sp`、不相等的 D/C 类型及缺少独立语法的
block-scaled kind 不能选中 variant。Selector 1–3 在 source 与缓存一致的实际 owned
修改中分别触发范围检查。不新增别名，也不增加可修改的生成 kind/order 字段。

Owned fragment 数量、物理标量宽度、metadata 绑定 identity、source 关联、AST 销毁后
的 copy/clone 以及 qualified target 保持为独立控制。检查 container 类型或 metadata
寄存器 identity 不等于认证运行时 low-bit packing 或 ordered metadata 内容；这些
runtime 义务不在此次拼写解释范围内。

## Dense integer WGMMA N boundary

Dense integer WGMMA 包含 `m64n240k32` 与 `m64n256k32`。
[指令小节](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions-wgmma-mma)
中的 integer Syntax 列表结束于 N=224，但
[shape 表格](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-shape)
明确包含 N=240/256，且 [integer fragment 契约](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-register-fragment-wgmma-64n32)
一致要求 N/2 个 S32 D 寄存器。本项目针对这一不完整的 Syntax 枚举，采用独立的
shape/fragment 契约。这是有界工程解释，并非厂商发布的勘误，也不建立表格普遍优先
的规则。另见 [WGMMA coverage](wgmma_coverage.md)。

Integer N 网格为 `8,16,24,32,48,64,...,256`，32 之后步长为 16。N=240 使用
120 个标量 S32 D 寄存器，N=256 使用 128 个。Integer N=232、248 虽是八的倍数，
仍不在网格中；N=264 超出范围。Dense integer K 保持为 32。Floating 与 sparse 家族
保留各自独立的网格和 selector 域。

两个边界尺寸保留四种 S8/U8 A/B 类型配对、两种 A placement（shared descriptor 或
四个 B32 寄存器 lane）及 plain/satfinite 形式，共 32 个独立 canonical identity。
两种 placement 中 B 都是 B64 shared descriptor。D 为 read/write。128-lane
accumulator 使用显式标量 brace pack，不授予普通 named-vector 权限。同符号形式保留
PTX 8.0 门槛，混合符号形式保留 PTX 8.4 门槛，全部要求精确 `sm_90a`。
Generic `sm_90` 或数值更高的 SM profile 不继承这一 target 资格。

Canonical 规则已包含这些 identity。
[`test_wgmma_coverage.cpp`](../../../submod/resolved_ir/test/test_wgmma_coverage.cpp)
中的定向 C++ 回归运行全部 32 个 source template，检查精确类和静态 topology，在
parser/AST 销毁后独立修改实际 owned fragment 数量、layout tag 与 descriptor
carrier。Source 数量 119/121、127/129 均被拒绝；独立的当前 129-lane 检查保留有界
view 访问。Copy/clone、外部 source 关联和配对 target/version 控制分别保留证据。
标准 normalization harness 独立检查精确的 32-key projection。Fence/commit/wait
identity 保持不变；这些检查不认证 descriptor 内容、物理 fragment packing、动态
protocol completion 或 GPU 结果。

## K96 encoded scale IDs and physical layout facts

Dense MX4 与 MX NV K96 采用的 Table 47 instruction-word scale A/B ID 域为
**{0,2}**，拒绝 **1/3**。A 位于 bits 29–30，B 位于 bits 4–5，各为无量纲的两位
selection code；bit 31 为 dense K choice。Deep K96 图以 `SFA_ID`/`SFB_ID` 标注
0/1/2/3 位置，冲突的 1/3 标签不扩大序列化字段域。本项目对 encoding 采用独立
instruction-descriptor 表的优先性，对物理布局事实则采用 deep 条款。这一工程解释
保留原文差异，并非 NVIDIA 发布的勘误。另见 [MX4](tcgen_mma_mx4_coverage.md)、
[MX NV](tcgen_mma_mxnv_coverage.md) 与 [family summary](tcgen_mma_family_coverage.md)。

三个量保持独立：

- **Encoded ID：** 无量纲 word-field code，独立检查为 0/2。
- **图示物理 layout index：** 图中的位置标签，没有已建立的 encoded ID 数值转换。
- **Alignment guarantee：** 调用方对所选 sub-column 作出的字节单位保证。

K96 block32 有三个 factor，A 为 `Mx3`、B 为 `3xN`；NV block16 有六个 factor，
A 为 `Mx6`、B 为 `6xN`。两者均要求四字节 sub-column alignment guarantee，保留
`LAYOUT_DEFINED_PLACEMENT`。可接受更强的正二次幂保证，例如 8 或 16 字节。
Encoded ID 2 配 alignment 4 或 8 有效：**不得**计算 `ID % alignment`、把 ID 乘成
字节偏移、令 encoded index 等于 physical index，或将禁止的 1/3 归一化为 0/2。
不引入新的 encoded-to-physical mapper 或 allocation 规则。

提供 alignment 0、2、3 是已知无效保证；省略 role、layout identity 或 alignment
则保留显式 missing obligation，不当作零，也不代表完整有效性。Report 可以接受所有
已提供事实，同时保留 layout/live-content 缺失义务。正确字段与调用方 layout 事实不
认证 opaque source `idesc` 内容、实际 packing、scale 内容、allocation 或 completion。
Word/facts 的值拷贝与 owned-source 拷贝保持独立。

已知 dense K96 保持 group 2、M256、PTX 8.8 起的精确 `sm_103a` 及既有 scale-type
限制。已知 K96 搭配书写的 `.scale_vec::2X` 或 `.scale_vec::4X` 仍与这些 vector
形式的精确 `sm_100a` 要求没有 target 交集，并保留未确定的物理 layout-rule 义务。
Table 59 的 factor count 不产生缺失的物理 row。K 未知时 source vector 形式仍可在
`sm_100a` 合法。本解释不增加 selector 拼写、target 权限、word authentication 或
可选 Tensor solver。

既有代码已表达此决策。定向 C++/Python 回归检查两组字段 mask、三个 block row、
ID/alignment 组合、错误 layout、缺失事实与 source/known-word 边界。Live contents
和 GPU execution 不在这些检查范围内。
