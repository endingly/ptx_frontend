# PTX 9.3 Video 指令覆盖

固定 [CUDA 13.3 / PTX 9.3 Video 章节](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#video-instructions)定义 23 个 opcode，以 23 个 typed variant 和 39 个 operand layout 表示。Canonical `video.yaml` 拥有封闭的类型、修饰符及操作数契约；生成的具体类公开 `video_lanes` 和 `video_operation`。`VideoType::{S32,U32}` 与普通整数算术类型保持独立。

| 家族 | Opcode | 最低 PTX / SM |
| --- | --- | --- |
| Scalar | `vadd`、`vsub`、`vmad`、`vabsdiff`、`vmin`、`vmax`、`vshl`、`vshr`、`vset` | 2.0 / 20 |
| 双 lane | `vadd2`、`vsub2`、`vavrg2`、`vabsdiff2`、`vmin2`、`vmax2`、`vset2` | 3.0 / 30 |
| 四 lane | `vadd4`、`vsub4`、`vavrg4`、`vabsdiff4`、`vmin4`、`vmax4`、`vset4` | 3.0 / 30 |

规范没有最大目标或移除 gate。较旧最低目标属于 frontend 契约测试；当前 assembler 对这些目标的支持是独立工具限制。SM80/100 的现代形式仍接受。拒绝 `u8/s8/u16/s16` 窄类型别名。

寄存器 carrier 必须是 scalar、general、32 位整数或 bit 存储。Scalar source 还允许整数常量，复用既有 32 位转换并保留原始 64 位来源；SIMD source 必须是寄存器。拒绝 float、vector、predicate、sink、special register 和 symbol。立即数不能带 selector。

普通向量显式 `.xyzw/.rgba` component 是有效 scalar carrier，可用于既有 role
接受普通 32 位寄存器的位置，包括允许的 `vmad` 寄存器 minus。Component selector
不是 VIDEO `.b0/.h0` 选择，其 VIDEO selector 仍为空。Merge destination 仍要求
实际 VIDEO selector；拒绝 chained `V.x.b0`、hardware component 与不允许的 minus。
Owned 与 standalone 校验将 component child range 同外层 minus 分开保留，并拒绝
伪造的 component-plus-VIDEO-selector 组合。此 frontend domain 遵循 ISA scalar-carrier
契约；有界 CUDA 13.3 V13.3.33 probe 即使初始化输入仍拒绝所测 `vadd V.x`、
`vmad -V.x`，而普通 `add V.x` 与 negated scalar `vmad` control 可汇编。
这是 compiler 兼容性差异，不代表 GPU 行为或完整 assembler parity。

`ResolvedVideoOperand` 拥有带位置的 register-or-immediate、可选 typed selector、寄存器取负标记及 minus range。Scalar byte selector 为 `b0..b3`，halfword 为 `h0..h1`。三操作数形式的 destination 无 selector；四操作数 secondary 形式使用 `.add/.min/.max`，merge 形式必须有 destination selector 且没有 secondary。Scalar saturation 可与 secondary 组合。Shift 必须在可选 `.sat` 后写 `.clamp/.wrap`，btype 固定 unsigned。`vset` 只有两个 source type 和 comparison，destination/C 按 unsigned 解释。

Packed source swizzle 索引连接后的 A+B carrier：双 lane digit 为 `0..3`，四 lane 为 `0..7`，source 允许重复。数组保留书写的高位到低位 digit 顺序。Destination mask 是非空、降序、无重复子集，`.add` 累加也允许 mask。默认 A=`h10`/`b3210`，B=`h32`/`b7654`，destination=`h10`/`b3210`。`video_effective_selector` 返回 typed 默认值，owned operand 仍保留省略状态。SIMD saturation 不能与 `.add` 并用；`vset2/4` 没有 saturation。

`vmad` 保留每个 source 的书写 minus、可选 `.po`、saturation 及 `.shr7/.shr15`。A/B 可用 scalar selector，D/C 是完整 carrier。Product minus 为 A-minus XOR B-minus，不能与 C-minus 同时有效。`.po` 禁止所有书写的寄存器 minus，包括相互抵消的一对。负数常量是值，不参与这些控制。C 常量使用 B32 carrier 转换。`video_mad_interpretation` 根据 source type 和寄存器控制推导 product/input-C/final 符号，不执行算术，也不改变独立书写的 dtype。

明确处理两处手册不一致：`vset2/4` 未覆盖 lane 按 syntax/pseudocode 使用提供的 C，邻近 prose 却写 B；`vmad` input-C 按 Description 的 intermediate sign 解释，而 pseudocode 内部 c128 扩展使用 final sign。公开 sign query 描述算术解释，不描述该内部扩展动作。拒绝 SIMD comparison 的非规范 `.max` 示例、重复或逆序 destination mask 及 `varvg` 拼写。

安装后的 [Video consumer](../../examples/video_consumer/main.cpp) 演示 AST 销毁后的所有权、selector/default、寄存器 minus 与负数字面量区别、符号推导及 mutable IR 拒绝。Frontend 测试覆盖每个 opcode、合法类型和控制组合、目标边界、绑定与来源、非法 mutation。CUDA 13.3.73 `ptxas` 静态 probe 接受全部 23 个代表性丰富形式，拒绝 SIMD saturation-plus-add 和带 saturation 的 SIMD comparison。这些证据建立 frontend/assembler 静态合法性；算术执行和 GPU 观察不属于 frontend 契约。
