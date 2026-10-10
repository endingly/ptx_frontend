# 纹理与 opaque 资源覆盖

前端以归档的 [CUDA 13.3 PTX ISA 9.3](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 为准，特别是 §5.3 与 §9.7.11。`tex`、`tld4`、`txq`、`istypep` 使用 canonical typed form。YAML 拼写先归一化为 mipmap mode、gather 分量、query 和资源 role 的闭合 Python enum 域，再生成 C++。resolved form 保留精确几何、mipmap 修饰符省略与显式写出之别、查询项或 gather 分量、选中类型和实际写出的可选操作数。`Instruction::check()` 重验可变操作数形状、纹理内部 lane 与资源 carrier；module 校验在语法树释放后继续检查绑定、texturing mode 和 feature gate。

| 指令族 | Typed form |
| --- | --- |
| `tex` | 1d、2d、3d、a1d、a2d、cube、acube、2dms、a2dms；省略/base mipmap，以及合法的 level/gradient；v4 u32/s32/f16/f32 或 v2 f16x2 结果；合法的 offset、深度比较与 residency。 |
| `tld4` | 从 2d、a2d、cube 或 acube 选择 r/g/b/a 分量；v4 u32/s32/f32 结果；合法的 offset、比较与 residency。 |
| `txq` | 纹理尺寸、通道格式、坐标模式、数组/mipmap/sample 数量；sampler 过滤/寻址/非归一化坐标查询；仅尺寸支持 `.level`。 |
| `istypep` | 对 texref、samplerref、surfref 做运行时 u64 兼容 handle 测试，不预设 handle 的实际运行时种类。 |

Unified mode 接受直接纹理声明或标量 u64 兼容寄存器中的间接资源。Independent mode 的访问要求分别直接引用纹理和 sampler 声明。Independent mode 的 sampler 查询使用 sampler 声明；`force_unnormalized_coords` 仅属于 independent mode。一个显式 texturing-mode 选择适用于模块的所有 target region；未指定的 region 继承该模式。Owned module 在变更后重检该约束，replacement source 验证使用新 source 的模块模式。间接资源使用要求 PTX 3.1 / sm20；可选控制和每个精确 form 各自保留 canonical feature gate。

Opaque `.global .texref/.samplerref/.surfref` 对象与 entry 输入参数保留 symbol identity、种类、源码形状、显式 alignment。静态命名成员仅属于 module-global opaque storage。前端不虚构字节大小、默认 alignment 或普通地址。兼容用的 `.tex .u32/.u64` 声明保留原拼写并映射为纹理 identity。`mov.u64` 从 opaque symbol 取得的是这一 identity，而非普通 storage address。静态成员赋值保留顺序和省略状态。Opaque array 初始化采用嵌套 aggregate 的对象索引路径与每个对象的命名成员组；这是通用 aggregate 语法和 §5.3 成员初始化的组合，§5.3 直接给出 scalar 示例，但未单列 opaque-array 示例。

坐标保留每个 source lane 及其位置。数组 layer 与 multisample index lane 在混合 32 位 tuple 中按 u32 解释；空间 lane 遵循选中的 coordinate type，被忽略的 padding lane 仍可观察。普通 1d 坐标可以写成 scalar 或 singleton pack；a1d 另有数组 layer lane。可选 offset 与 gradient 使用 brace pack（一维也用 singleton pack），每个 lane 可为 register 或 typed immediate，且可混用。Offset immediate 保留原始 source 值并须位于 [-8, 7]；深度比较使用 scalar。`tex` 还保留归档 Notes 允许的省略方括号拼写以及所有几何的 v4 坐标兼容形式。当前 CUDA 13.3 assembler 拒绝一份测试过的省略方括号源码，尽管归档 Notes 允许。归档 a2d offset 示例缺少所需 padding lane；前端遵循四 lane 操作描述，assembler 也支持该形式。a2dms 的 lane role 遵循操作段与示例：layer、sample、x、y。`tld4` 的 unified a2d/cube/acube 形式依据 Description 与 ISA Notes，CUDA 13.3 assembler 也接受，尽管 Syntax 行更窄。

前端校验静态可知的源码事实，并拒绝已知越界 offset 常量。它不解码纹理对象、不证明间接寄存器的运行时种类、不从命名初始化推断配置，也不执行采样。运行时纹理配置、寻址模式、坐标界限、寄存器中的 offset 值、LOD 与 residency 仍由程序保证。
