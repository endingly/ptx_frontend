# Surface 指令覆盖

前端遵循归档的 [CUDA 13.3 PTX ISA 9.3 surface 指令](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#surface-instructions)。Canonical `surface.yaml` 描述 `suld`、`sust`、`sured` 与 `suq`，340 个 typed form 覆盖 1,864 种合法的 mnemonic 修饰符组合：825 种 load、852 种 store、180 种 reduction 与 7 种 query。类型与可选 cache 的选择共用 typed form；组合计数不是生成 class 的数量。

| 指令族 | 源码覆盖 |
| --- | --- |
| `suld.b` | 1d、2d、3d、a1d、a2d；scalar/v2/v4 b8/b16/b32 与 scalar/v2 b64；省略/ca/cg/cs/cv cache；trap/clamp/zero。 |
| `sust.b` | 相同的 geometry、vector 与 type 矩阵；省略/wb/cg/cs/wt cache；trap/clamp/zero。 |
| `sust.p` | 1d、2d、3d；scalar/v2/v4 b32；trap/clamp/zero；无 cache selector。 |
| `sured.b` | 1d、2d、3d；add u32/u64/s32，min/max u32/s32/u64/s64，and/or b32；trap/clamp/zero；scalar data，无 cache selector。 |
| `sured.p` | 1d、2d、3d；add/min/max/and/or b32 与 min/max b64；trap/clamp/zero；scalar data，无 cache selector。 |
| `suq` | width、height、depth、channel_data_type、channel_order、array_size、memory_layout；b32 结果。 |

Surface access 使用独立的 typed descriptor 与 owned operand。`SurfaceGeometry`、`SurfaceAddressingMode`（Byte/Sample）、`SurfaceBoundaryMode`、`SurfaceReductionOperation` 与 `SurfaceQuery` 是闭合域。操作数复用既有 opaque `Surface` identity，支持 module-global `.surfref`、opaque entry 输入参数及 scalar 64 位 integer/bit 间接 handle，不继承 texture mode、sampler、cube、mipmap、offset 或 residency 规则。Unified 与 independent texturing 模块均可使用 surface 指令。`mov.u64` 保留资源 identity；直接 surface 操作数不会退化为 generic address。

坐标保留每个 lane 的源码值与位置。1d 接受 scalar 或 singleton brace pack；2d 与 a1d 要求两个 lane；3d 与 a2d 要求四个 lane。Spatial lane 按 s32 解释，array 首 lane 按 u32 解释，3d/a2d 最后一个 padding lane 保留且忽略。Scalar 32 位 integer/bit 寄存器兼容这些解释。坐标可为 typed immediate 或 register。Load 使用普通 register/register-vector destination；store 与 reduction 使用普通 register-or-immediate 或 value-vector source，store vector 可逐 lane 混用 register/immediate。窄 b8/b16 data 沿用前端既有 carrier 兼容策略，可使用 native 或更宽的兼容寄存器，CUDA 13.3.73 `ptxas` 也支持；surface 条文本身未单独规定 widening。传输宽度遵循指令 data type，与 carrier 宽度独立。数据仍受普通 shape/type 检查。

通用 [vector 规则](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#vectors) 将 surface data vector 限为 128 位，因此 load/store 均拒绝 v4.b64；`ld` 的指令专属扩展不适用于 surface。Canonical normalization 在生成 C++ 前拒绝非法 opcode/addressing/geometry/vector/type/reduction/query 组合与被削弱的 feature gate。`Instruction::check()` 重验可变选中类型、坐标 role 与 tuple shape、资源 kind/carrier、必要方括号及 availability。Module validation 在语法树释放后继续检查直接绑定及每个嵌套坐标/资源引用，也涵盖 replacement-source validation。

Byte load/store 从 PTX 1.5 起支持。Cache selector 与 clamp/zero 要求 PTX 2.0 / sm20；byte 3d/array access 要求 PTX 3.0 / sm20。Formatted store 与 reduction 从 PTX 2.0 / sm20 起支持，包括 `sust.p.3d`。所有间接资源要求 PTX 3.1 / sm20。64 位 min/max reduction 要求 PTX 8.1 / sm50。Query floor：尺寸为 PTX 1.5，channel property 为 2.1，array_size 为 4.1，memory_layout 为 4.2；direct query 无额外 SM floor。

显式 generic `sm_50` target profile 的 enabled-family 与 capability 域均为空。完整模块 fixture 覆盖 PTX 8.1 / sm50 的全部 64 位 min/max form，并拒绝 PTX 8.0 / sm50 与 PTX 8.1 / sm30；该 profile 不代表具备现代 target capability。

当示例与 ISA Syntax 不一致时，以 Syntax 为准：`sured.p.min.u32` 示例不会扩展 formatted reduction 类型矩阵；省略必要 boundary modifier 的 surface array 示例也不授权这种省略。CUDA 13.3.73 `ptxas` 拒绝这些拼写。它的 parser 也拒绝测试过的裸 `[surface,0]` 坐标，但文档 1d scalar 坐标规则与通用 scalar constant 允许该形式，因此前端与 `[surface,{0}]` 一并保留。

校验覆盖源码与静态前端事实，不解码间接对象、不证明其运行时资源种类、不从 named member 推断 surface 配置，不执行 formatted conversion 或 reduction，也不证明坐标边界、运行时寄存器自然对齐、format 兼容性、aliasing、memory effect 或 boundary behavior。运行时资源配置与访问有效性仍由程序保证。[安装包 surface consumer](../../examples/surface_consumer/main.cpp) 验证公共 typed header 与语法树释放后的校验。
