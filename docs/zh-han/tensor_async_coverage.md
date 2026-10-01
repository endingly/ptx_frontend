# Tensor 异步数据移动

Frontend 支持 [PTX ISA 9.3 §5.5 与 §9.7.9.26.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 中的 tiled tensor-map 形式。复合操作数 `[tensorMap, {coords}]` 在 CST 与 AST 中保留嵌套地址、坐标元素、标点及源码范围。Resolved IR 自有 `ResolvedTensorMapRef` 和 rank 1–5 的 `ResolvedTensorOperand`。20 个 copy/prefetch variant 和 40 个 reduction variant 都接受省略或显式 `.tile`；`tile` 字段保留源码是否写出该限定符。

| 形式 | 操作数与完成机制 | 最低 PTX / 目标 |
| --- | --- | --- |
| `cp.async.bulk.prefetch.tensor.{1d…5d}.L2.global{.tile}` | `[tensorMap, {coords}]`；无 completion 操作数 | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cluster.global{.tile}.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {coords}], [mbar]`；mbarrier complete-tx-bytes | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cluster.global{.tile|.tile::gather4 (2d)|.im2col* (3d…5d)}.mbarrier::complete_tx::bytes.multicast::cluster` | `[dst], [tensorMap, {coords}], [mbar]{, im2colInfo}, ctaMask`；必须提供 mask，若有 info 则 mask 位于其后 | 基础门槛 8.0 / SM 90；仍需满足各模式的限定门槛 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cta.global{.tile}.mbarrier::complete_tx::bytes` | 相同操作数，CTA shared 目的地 | 8.6 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.global.shared::cta{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |
| `cp.reduce.async.bulk.tensor.{1d…5d}.global.shared::cta.{add,min,max,inc,dec,and,or,xor}{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{3d…5d}.global.shared::cta.im2col_no_offs.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |
| `cp.reduce.async.bulk.tensor.{3d…5d}.global.shared::cta.{add,min,max,inc,dec,and,or,xor}.im2col_no_offs.bulk_group` | 相同操作数和完成机制，沿用 reduction 类型的条件约束 | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{3d…5d}.shared::{cluster,cta}.global.{im2col,im2col::w,im2col::w::128}.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {coords}], [mbar]{, im2colInfo}` | 依模式与目的地而定，见下文 |
| `cp.async.bulk.prefetch.tensor.{3d…5d}.L2.global.{im2col,im2col::w,im2col::w::128}` | `[tensorMap, {coords}]{, im2colInfo}`；无 completion 操作数 | 依模式而定，见下文 |
| `cp.async.bulk.tensor.2d.shared::{cta,cluster}.global.tile::gather4.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {column,row0,row1,row2,row3}], [mbar]` | CTA：8.6 / catalog SM ≥100；cluster：见下文限定门槛 |
| `cp.async.bulk.prefetch.tensor.2d.L2.global.tile::gather4` | `[tensorMap, {column,row0,row1,row2,row3}]`；无 completion 操作数 | 见下文限定门槛 |
| `cp.async.bulk.tensor.2d.global.shared::cta.tile::scatter4.bulk_group` | `[tensorMap, {column,row0,row1,row2,row3}], [src]`；bulk group | 见下文限定门槛 |

新增的 27 个 no-offset store/reduction 形式使用显式固定的 `.im2col_no_offs` 模式。生成的绑定携带封闭的预期 `TensorAccessMode`，owned tensor 操作数在语法树销毁后仍保留该模式。原有 60 个 tiled 形式即使省略 `.tile` 也保持 `Tiled`；非 tensor 形式没有 tensor 模式。选中的 no-offset 形式及 owned 模式保留固定限定符的身份，但不单独提供该固定 token 的源码位置，也不承诺逐字节重建源码。该模式没有 `im2colInfo` 操作数。

另有 27 个 im2col read 身份：rank 3–5 上的 CTA load、cluster load、L2 prefetch 分别使用固定 `.im2col`、`.im2col::w` 或 `.im2col::w::128`。每个身份都有带与不带 `im2colInfo` 的布局；省略即明确缺席，不构造零向量，也不声称已知执行默认值。带信息的 `ResolvedTensorIm2colInfo` 在 AST 销毁后仍自有寄存器/立即数元素、元素源码范围和整个花括号范围。`tensor_im2col_info_role(tensor, info, index)` 从已选模式和 rank 返回类型化 W/H/D offset 或 halo/offset 角色；不适用或元数据不合格时无返回值。普通 `.im2col` 在 rank 3/4/5 分别采用 1/2/3 个 W/H/D offset，值上限分别为 65535/255/31。两个 W 模式采用 `{wHalo,wOffset}`：`.im2col::w` 的 halo 上限 511，`::w::128` 的 halo 上限 31，offset 上限 31。整数常量在该操作数使用处先窄化到 unsigned 16 位，再检查这些界限；原始 64 位源码位与符号保留。General 类标量 B16/U16/S16 寄存器可作载体，其运行时值仍需满足界限。

四个 gather/scatter 身份均为 rank 2，但各自有**五个**有序坐标：一个列索引和四个独立行索引。生成的绑定将预期 tensor rank 与坐标数量分别记录。`tensor_gather_scatter_coordinate_role(tensor,index)` 仅在模式、rank、数量和索引有效时返回自有的 `Column` 或 `Row0`–`Row3` 值。PTX 8.7 将旧的“四个 x、一个 y”措辞明确为 `{x,y0,y1,y2,y3}`；角色 API 遵循指令操作数表的顺序，不推断 descriptor 的维度编号。固定模式没有 `im2colInfo`。Scatter 拒绝任一位置在 S32 转换后为负的字面量，gather read 可以保留该值。寄存器运行时值、边界、descriptor rank、四行 box 的构造、box 维度 1 等于一及非 interleave 布局，仍是运行时或不透明 descriptor 约束。

CTA gather 从 PTX 8.6 起支持 catalog 中 SM ≥100 的目标（含有效 generic profile）。Cluster gather、gather prefetch 和 scatter 要求 PTX 8.6 的精确 `sm_100a`、8.8 起的 `sm_100f` family，或 9.0 起的 `sm_110f` family。Catalog 继承按各自门槛包含 `sm_103a/f` 和 `sm_110a/f`；generic 与 `sm_120` family 不满足这些限定形式。Gather load 复用 mbarrier complete-tx-bytes，prefetch 无完成操作数，scatter 使用 BulkGroup。指针元数据无法证明的 CTA 本地性、远端 cluster/barrier 关系仍须由调用方保证。

普通 im2col cluster load 与 prefetch 从 PTX 8.0 / SM 90 开始；CTA load 从 PTX 8.6 / SM 90 开始。W 模式从 PTX 8.6 开始。`.im2col::w` CTA load 允许 catalog 中 SM 100 或更高的目标，包括 generic profile；cluster load 允许 PTX 8.6 的精确 `sm_100a`、从 PTX 8.8 起的 `sm_100f` family，或从 PTX 9.0 起的 `sm_110f` family。指令本地的目标段落只列出前两者，PTX 9.3 Table 63 明确补充 W cluster load 的 `sm_110f` family。W128 load 与两个 W prefetch 使用相同的三个精确目标/family 分支；family 继承依目标 catalog 判定。W 模式要求 descriptor 已设置 swizzle，并排除 128-byte swizzle 加 32-byte atomicity 与 8-byte flip 的组合。Descriptor 内容不透明，因此该限制、filter/bounding-box 兼容性及省略 info 的执行含义都是待满足义务，不能被静态推断为已证明。

本前端遵循固定 PTX 9.3 手册的语法和数值界限。在完整 module 探针中，ptxas 13.3.73 接受了全部 27 个带 info 的 im2col read 形式，却以 `Arguments mismatch` 拒绝了全部 27 个省略 info 的形式；前端仍按手册的正式语法允许省略。汇编器也接受了三个超出前端 U16 转换后界限的测试值（普通 rank 4 offset 256、`.im2col::w` halo 512、`.im2col::w::128` halo 32）。PTX 9.0 / `sm_110a` W cluster load 的汇编器接受结果印证了 Table 63 的 `sm_110f` family 条款，并非与整部固定手册相冲突。省略 info 后的执行含义仍未知。汇编器的结果不能证明 GPU 运行时有效、descriptor 兼容或同步正确。

77 个完整 module 的 ptxas 13.3.73 gather/scatter 探针接受全部四个规范形式和四个所测示例顺序限定符形式；目标结果印证 CTA 数值门槛及三个限定目标分支。汇编器以 `Arguments mismatch` 拒绝四种形式中的 unsigned-64 最大值坐标常量，但前端保留合法整数源码并在使用处窄化到 S32。反过来，汇编器接受转换后 S32 为负（`0xffffffff`）的 scatter 坐标；前端继续执行手册的 tensor-write 非负约束。PTX 8.9 不是汇编器支持的 module 版本，探针使用其两侧支持的 8.8 和 9.0 端点。汇编通过不能证明 descriptor 布局、边界、同步或 GPU 执行。

`tensorMap` 是指向 opaque 128 字节 descriptor 的 generic pointer。直接 descriptor 符号可位于 kernel `.param`、`.const` 或 `.global`，其声明身份、存储空间、对齐及源码范围保留在 owned IR。寄存器指针保留寄存器身份，但运行时来源与对齐未知。Tensor 数据方向单独建模：load 将 global tensor 数据写入 shared，store 从 CTA shared 读取并写入 global tensor 数据。

Checker 分别比较 owned rank 与生成的固定 rank、以及准确的坐标数量。既有模式每个 rank 对应一个 signed-32 坐标；gather/scatter 为 rank 2、五个坐标。坐标接受兼容的 32-bit 标量寄存器。PTX 的 64-bit 整数常量在坐标使用处转换为 signed 32-bit，原始值及符号仍保留在 owned IR 中：`4294967296` 转换为零，`4294967295` 转换为 −1。Load 与 prefetch 可使用转换后为负的坐标；store、scatter 与 reduction 拒绝静态转换后为负的坐标，寄存器坐标值须在运行时检查。已知 descriptor、shared 数据、mbarrier 地址分别要求 64、16、8 字节对齐。Shared 数据检查采用 tiled mode 的基线；descriptor 不透明，依赖 swizzle mode 的更强对齐仍需运行时保证。未知寄存器指针对齐不视为已证明。已知 shared/local 或非 kernel parameter descriptor 会被拒绝，并检查 PTX/SM 可用性。

Reduction 的固定操作由生成的 `TensorReductionOp` 标识；`tensor_reduction_accepts_element_type` 查询 descriptor 元素类型的条件兼容性。`add` 允许 U32/S32/U64/F32/F16/BF16；`min` 与 `max` 允许 U32/S32/U64/S64/F16/BF16；`inc` 与 `dec` 仅允许 U32；位操作 `and`、`or`、`xor` 允许 B32/B64。查询不读取 descriptor 内容，因此不能证明某个 opaque descriptor 的实际类型。Reduction 复用 bulk-group 完成机制，没有 mbarrier 操作数或字节计数。Descriptor 内容、操作与类型的一致性、swizzle、stride、bounds 及运行时排序仍由调用方保证。PTX 语法与 completion 段落规定 bulk-group，尽管描述性文字中另有一处与之冲突的 mbarrier 语句。

全部 73 个 tensor-write 形式、27 个 im2col read 身份、三个 gather read 与 15 个 multicast cluster read 重新检查 owned 指针载体是否为标量 32/64 位整数/位类型，包括向量形状及已知类型。Gather/scatter 还在 AST 销毁后重新检查全部五个标量坐标载体。已知 map read 可来自 global/const/kernel input param，要求 64 字节对齐；已知 shared 数据和 mbarrier 地址分别要求 16 与 8 字节对齐。未知指针的存储空间和对齐仍是运行时义务。此切片无法静态证明 descriptor 内容、descriptor 内 rank 一致性、swizzle、stride、bounds、barrier locality 或运行时同步。L2 cache policy/hint 尚不在支持范围；相邻但未支持的拼写会在形式选择时失败。

15 个 cluster-load multicast 身份对应 24 个物理布局：五个 tiled rank、一个 rank-two gather，以及 rank 3–5 的三个 im2col 模式（各有 info 存在/省略两种布局）。固定 multicast 限定符必须带有末尾的 16 位 `ctaMask`；没有限定符时不能带 mask。每一位选择一个目标 `%cluster_ctarank`，数据复制到所选 CTA shared 内存的相同偏移。生成的 variant 保留固定 multicast 身份，操作数 descriptor 标注自有 mask 角色。前端接受标量 B16/U16/S16 mask 和在 U16 使用处窄化的整数字面量，并保留原始 64 位源码位与符号。已知 B32/B64 寄存器声明以及向量、predicate 寄存器均被拒绝。动态 mask 内容、所选 CTA 的存在性、barrier 本地性及 descriptor 内容仍是运行时义务；前端不静态禁止零或超出当前 cluster 的位。现有模式、rank、坐标、info pack、已知 64/16/8 字节对齐及 complete-tx-bytes 检查继续适用。ISA 的优化目标列表是性能建议，不是额外的可用性门槛。L2 cache hint/policy 仍在本子集之外。

官方 ptxas 13.3.73 另外接受所测的 B64 源：普通 im2col rank 3（PTX 8.0 / sm_90）、im2col::w rank 3（8.6 / sm_100a）和 im2col::w::128 rank 3（8.8 / sm_103f）；tile rank 1（8.0 / sm_90）及 gather rank 2（8.6 / sm_100a）则被拒绝。前端仍遵守规定的 16 位寄存器契约。汇编通过不能证明较宽源在运行时如何被使用。原始 corpus 有 103 条记录：102 次汇编调用及一条因错误 prefetch 对照而跳过的配对项；后续八次调用修正了普通 tiled-prefetch 对照，并确认 prefetch 上的 multicast 被拒绝。原始九条 im2col-info 省略拒绝属于既有正式语法/工具差异，不是 mask 失败。该 corpus 不能证明 GPU 执行或更宽寄存器的穷尽行为。

## 显式 tensor-copy CTA group

45 个 grouped tensor-load 身份新增 72 个操作数布局：CTA load、cluster load 和 cluster-multicast load 各 15 个身份。每组包括 rank 1–5 的 tiled、rank-two gather4，以及 rank 3–5 的三个 im2col 模式；九个 im2col 身份各自保留 info 存在/省略两种布局。每个新 variant 以一个**必须写出、带源码位置、封闭类型**的 `cta_group::{1|2}` 字段表示两种值。原有 133 个 tensor 身份和 169 个布局继续保持省略拼写。合计 178 个规范 tensor 身份、241 个布局；按省略/一/二展开后是 223 个身份组合、313 个布局与 group 的组合。省略 group 在适用 load 上生效为一，但仍区别于写出的 `::1`。Store、reduction 和 prefetch 不具有 CTA-group 角色。

生成的 `tensor_cta_group_role()` 查询在写出 group 时返回自有的值及 token 范围，并给出有效 One/Two 与 mbarrier 信号路由；对非 load 或损坏的 group 无返回值。前端输出手册中的规范顺序 `completion.multicast::cluster.cta_group::{1|2}`，还接受唯一声明的相邻交换别名 `completion.cta_group::{1|2}.multicast::cluster`。别名选中相同的类型化 variant，并保留相同的 group/multicast 槽身份与源码范围；它不开放任意顺序，也不改变 mask 位于可选 info 后的操作数顺序。Group 不增加操作数、不改变 `mbarrier::complete_tx::bytes` 的计数，也不引入 TCGEN05 单函数内 group 值一致性的规则。同一函数可混用省略、`::1` 和 `::2` 的 tensor load。

两个写出值均要求 PTX 8.6 的精确 `sm_100a`、PTX 8.8 的 `sm_100f` family，或 PTX 9.0 的 `sm_110f` family，并与基础模式和目的地门槛求交。`sm_103a/f`、`sm_110a` 等继承由目标 catalog 决定；generic 100/103/110 与 120 family 不支持。Group One 向目标 CTA 发信号；multicast 时向每个所选目标 CTA 的对应 mbarrier 偏移发信号。Group Two 可向目标 CTA 或 CTA-pair 中的 peer 发信号；multicast 按 barrier CTA 的奇偶性选择 odd/even peer。具体的 group-two peer 许可限定了手册前文对 `shared::cta` 的一般本地 barrier 措辞。静态声明只能证明空间、类型、形状和已知 map/目的地/barrier 的 64/16/8 字节对齐，不能证明真实 CTA 所有权、peer 关系、cluster 形状、mask 成员资格或计数完成；这些仍是运行时义务。

固定 ptxas 13.3.73 检查点共有 84 条记录：83 次调用、50 次接受、33 次拒绝和一条跳过的候选。30 个基础/带 info 的 load 对照全部汇编通过；18 个省略 info 的形式在匹配的带 info 对照通过后失败，属于已有的正式可选语法/工具差异。所测 PTX 9.3/`sm_110a` rank-three tiled cluster-multicast **group-one** 相邻交换样例及其匹配的规范 group-one 对照均通过。该观察支持这一项声明的别名，不能证明任意顺序或 GPU 执行。限定目标端点及匹配的无 group 对照通过；PTX 8.5 的目标指令与无 group 对照失败，因此没有独立证明 group 版本门槛。两条 group-two barrier module 中 shared 符号的名称不能证明本地或 peer 所有权。最初的路径夹具失败运行不计入上述结果。

## Tensor-map 字段替换与 proxy fence

已安装的[调用者已知 tensor-map 事实查询](tensor_map_known_facts.md)针对独立提供的
descriptor 事实与类型化选中 tensor 访问分别报告 22 项条件关系。它不解码不透明的
128 字节 descriptor，也不证明运行时边界或同步。

`tensormap.replace.tile` 支持全部 11 个字段：`global_address`、`rank`、`box_dim`、`global_dim`、`global_stride`、`element_stride`、`elemtype`、`interleave_layout`、`swizzle_mode`、`swizzle_atomicity`、`fill_mode`。前六个按字段类型接受 `.b32` 或 `.b64` 寄存器或整数常量；后五个只接受 Table 33 的立即数编码。可选 `.global` 或 `.shared::cta` 限定符必须与已知目的地址空间一致。替换目的地须为可写 global 或 CTA shared descriptor，不能沿用 const 或 param 的只读权限。已知类型的地址寄存器须为 32 或 64 位整数/位类型；proxy fence 的源与目的地址同样适用。生成的 variant 提供 `replacement_field`、保留地址和源码身份的自有拷贝 `tensor_map_ref()`，编码字段还提供按字段类型返回 optional 的 `encoded_value()`。这些投影不解码 128 字节 descriptor 内容。

`rank` 编码为维数减一。寄存器 rank 留待运行时验证；立即数在使用处转换为 `.b32`，转换后的编码必须为 0–4。其他有类型 `.b32`/`.b64` 替换常量同样在使用处窄化。维度及 stride 的可选 `ord` 要求原始源码整数为 0–4。Table 33 编码也要求原始源码值精确匹配；2³² 的倍数不能当作零编码的别名。五个封闭值域是元素类型（0–15）、interleave layout（0–2）、swizzle mode（0–4）、swizzle atomicity（0–3）、fill mode（0–1）。元素编码 15 是一个编码身份，在 tensor load 时解释为 `b6x16_p32`，在 tensor store 时解释为 `b6p2x16`。原始源码位、转换后位及原始符号保留在 owned IR，checker 可在修改后重新验证一致性。

基础替换有六个目标分支：精确 `sm_90a` 从 PTX 8.3、精确 `sm_100a` 从 8.6、精确 `sm_120a` 从 8.7、`sm_100f` family 从 8.8、`sm_110f` family 从 9.0、`sm_120f` family 从 8.8。Family 继承由显式 source-target catalog 决定；数字 SM 相同的 generic target 不满足要求。Swizzle atomicity 要求 PTX 8.6 且排除 `sm_90a`；元素编码 13–15 要求 PTX 8.7 且排除 `sm_90a`；swizzle-mode 编码 4（96 B）只允许 PTX 8.8 的精确 `sm_103a`。Checker 将字段及编码的约束与基础目标 catalog 取交集。当前 descriptor 内容及后续数据访问的兼容性是条件约束，仍需运行时保证。

`tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.{cta,cluster,gpu,sys}.sync.aligned [dst], [src], 128` 将 CTA shared tensor map 复制到 global，并把 generic proxy 的写入 release 给后续 tensor-map proxy 访问。它从 PTX 8.3 / SM 90 开始，复用 `MemoryScope` 和 `TensormapToGeneric`，没有异步 completion group；复制大小要求原始源码立即数精确为 128 字节；owned validation 还检查转换后的 `.u32` 位值仍为 128。替换对整个 1024-bit descriptor 执行 weak memory operation；结合自然访问大小对齐规则，已知目的地址需 128 字节对齐。Proxy copy 的两个已知地址也需 128 字节对齐。这是由手册条款推导的约束，不能归因于 assembler 的拒绝结果。未知寄存器地址的对齐仍为运行时义务。原先 tiled read 的 64 字节 descriptor 对齐不变。
