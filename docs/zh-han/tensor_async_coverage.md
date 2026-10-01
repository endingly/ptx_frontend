# Tensor 异步数据移动

Frontend 支持 [PTX ISA 9.3 §5.5 与 §9.7.9.26.5](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html) 中的 tiled tensor-map 形式。复合操作数 `[tensorMap, {coords}]` 在 CST 与 AST 中保留嵌套地址、坐标元素、标点及源码范围。Resolved IR 自有 `ResolvedTensorMapRef` 和 rank 1–5 的 `ResolvedTensorOperand`。20 个 copy/prefetch variant 和 40 个 reduction variant 都接受省略或显式 `.tile`；`tile` 字段保留源码是否写出该限定符。

| 形式 | 操作数与完成机制 | 最低 PTX / 目标 |
| --- | --- | --- |
| `cp.async.bulk.prefetch.tensor.{1d…5d}.L2.global{.tile}` | `[tensorMap, {coords}]`；无 completion 操作数 | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cluster.global{.tile}.mbarrier::complete_tx::bytes` | `[dst], [tensorMap, {coords}], [mbar]`；mbarrier complete-tx-bytes | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.shared::cta.global{.tile}.mbarrier::complete_tx::bytes` | 相同操作数，CTA shared 目的地 | 8.6 / SM 90 |
| `cp.async.bulk.tensor.{1d…5d}.global.shared::cta{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |
| `cp.reduce.async.bulk.tensor.{1d…5d}.global.shared::cta.{add,min,max,inc,dec,and,or,xor}{.tile}.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |
| `cp.async.bulk.tensor.{3d…5d}.global.shared::cta.im2col_no_offs.bulk_group` | `[tensorMap, {coords}], [src]`；复用 bulk-group commit/wait | 8.0 / SM 90 |
| `cp.reduce.async.bulk.tensor.{3d…5d}.global.shared::cta.{add,min,max,inc,dec,and,or,xor}.im2col_no_offs.bulk_group` | 相同操作数和完成机制，沿用 reduction 类型的条件约束 | 8.0 / SM 90 |

新增的 27 个 no-offset store/reduction 形式使用显式固定的 `.im2col_no_offs` 模式。生成的绑定携带封闭的预期 `TensorAccessMode`，owned tensor 操作数在语法树销毁后仍保留该模式。原有 60 个 tiled 形式即使省略 `.tile` 也保持 `Tiled`；非 tensor 形式没有 tensor 模式。选中的 no-offset 形式及 owned 模式保留固定限定符的身份，但不单独提供该固定 token 的源码位置，也不承诺逐字节重建源码。该模式没有 `im2colInfo` 操作数。

`tensorMap` 是指向 opaque 128 字节 descriptor 的 generic pointer。直接 descriptor 符号可位于 kernel `.param`、`.const` 或 `.global`，其声明身份、存储空间、对齐及源码范围保留在 owned IR。寄存器指针保留寄存器身份，但运行时来源与对齐未知。Tensor 数据方向单独建模：load 将 global tensor 数据写入 shared，store 从 CTA shared 读取并写入 global tensor 数据。

Checker 要求坐标数量恰好等于 rank，每个坐标具有 signed-32 语义，并接受兼容的 32-bit 寄存器。PTX 的 64-bit 整数常量在坐标使用处转换为 signed 32-bit，原始值及符号仍保留在 owned IR 中：`4294967296` 转换为零，`4294967295` 转换为 −1。Load 与 prefetch 可使用转换后为负的坐标；store 与 reduction 拒绝静态转换后为负的坐标，寄存器坐标值须在运行时检查。已知 descriptor、shared 数据、mbarrier 地址分别要求 64、16、8 字节对齐。Shared 数据检查采用 tiled mode 的基线；descriptor 不透明，依赖 swizzle mode 的更强对齐仍需运行时保证。未知寄存器指针对齐不视为已证明。已知 shared/local 或非 kernel parameter descriptor 会被拒绝，并检查 PTX/SM 可用性。

Reduction 的固定操作由生成的 `TensorReductionOp` 标识；`tensor_reduction_accepts_element_type` 查询 descriptor 元素类型的条件兼容性。`add` 允许 U32/S32/U64/F32/F16/BF16；`min` 与 `max` 允许 U32/S32/U64/S64/F16/BF16；`inc` 与 `dec` 仅允许 U32；位操作 `and`、`or`、`xor` 允许 B32/B64。查询不读取 descriptor 内容，因此不能证明某个 opaque descriptor 的实际类型。Reduction 复用 bulk-group 完成机制，没有 mbarrier 操作数或字节计数。Descriptor 内容、操作与类型的一致性、swizzle、stride、bounds 及运行时排序仍由调用方保证。PTX 语法与 completion 段落规定 bulk-group，尽管描述性文字中另有一处与之冲突的 mbarrier 语句。

全部 72 个 tensor-write 形式会重新检查 owned map 和 CTA source 是否为标量 32/64 位整数/位类型指针载体，包括向量形状及已知类型。已知 map read 可来自 global/const/kernel input param，要求 64 字节对齐；已知 CTA shared source 要求 16 字节对齐。未知指针的存储空间和对齐仍是运行时义务。此切片无法静态证明 descriptor 内容、descriptor 内 rank 一致性、swizzle、stride、bounds、barrier locality 或运行时同步。带 info 操作数的其他 im2col load/prefetch 模式、gather/scatter、multicast、显式 CTA group、cache policy/hint 尚不在支持范围；相邻但未支持的拼写会在形式选择时失败。

## Tensor-map 字段替换与 proxy fence

`tensormap.replace.tile` 支持全部 11 个字段：`global_address`、`rank`、`box_dim`、`global_dim`、`global_stride`、`element_stride`、`elemtype`、`interleave_layout`、`swizzle_mode`、`swizzle_atomicity`、`fill_mode`。前六个按字段类型接受 `.b32` 或 `.b64` 寄存器或整数常量；后五个只接受 Table 33 的立即数编码。可选 `.global` 或 `.shared::cta` 限定符必须与已知目的地址空间一致。替换目的地须为可写 global 或 CTA shared descriptor，不能沿用 const 或 param 的只读权限。已知类型的地址寄存器须为 32 或 64 位整数/位类型；proxy fence 的源与目的地址同样适用。生成的 variant 提供 `replacement_field`、保留地址和源码身份的自有拷贝 `tensor_map_ref()`，编码字段还提供按字段类型返回 optional 的 `encoded_value()`。这些投影不解码 128 字节 descriptor 内容。

`rank` 编码为维数减一。寄存器 rank 留待运行时验证；立即数在使用处转换为 `.b32`，转换后的编码必须为 0–4。其他有类型 `.b32`/`.b64` 替换常量同样在使用处窄化。维度及 stride 的可选 `ord` 要求原始源码整数为 0–4。Table 33 编码也要求原始源码值精确匹配；2³² 的倍数不能当作零编码的别名。五个封闭值域是元素类型（0–15）、interleave layout（0–2）、swizzle mode（0–4）、swizzle atomicity（0–3）、fill mode（0–1）。元素编码 15 是一个编码身份，在 tensor load 时解释为 `b6x16_p32`，在 tensor store 时解释为 `b6p2x16`。原始源码位、转换后位及原始符号保留在 owned IR，checker 可在修改后重新验证一致性。

基础替换有六个目标分支：精确 `sm_90a` 从 PTX 8.3、精确 `sm_100a` 从 8.6、精确 `sm_120a` 从 8.7、`sm_100f` family 从 8.8、`sm_110f` family 从 9.0、`sm_120f` family 从 8.8。Family 继承由显式 source-target catalog 决定；数字 SM 相同的 generic target 不满足要求。Swizzle atomicity 要求 PTX 8.6 且排除 `sm_90a`；元素编码 13–15 要求 PTX 8.7 且排除 `sm_90a`；swizzle-mode 编码 4（96 B）只允许 PTX 8.8 的精确 `sm_103a`。Checker 将字段及编码的约束与基础目标 catalog 取交集。当前 descriptor 内容及后续数据访问的兼容性是条件约束，仍需运行时保证。

`tensormap.cp_fenceproxy.global.shared::cta.tensormap::generic.release.{cta,cluster,gpu,sys}.sync.aligned [dst], [src], 128` 将 CTA shared tensor map 复制到 global，并把 generic proxy 的写入 release 给后续 tensor-map proxy 访问。它从 PTX 8.3 / SM 90 开始，复用 `MemoryScope` 和 `TensormapToGeneric`，没有异步 completion group；复制大小要求原始源码立即数精确为 128 字节；owned validation 还检查转换后的 `.u32` 位值仍为 128。替换对整个 1024-bit descriptor 执行 weak memory operation；结合自然访问大小对齐规则，已知目的地址需 128 字节对齐。Proxy copy 的两个已知地址也需 128 字节对齐。这是由手册条款推导的约束，不能归因于 assembler 的拒绝结果。未知寄存器地址的对齐仍为运行时义务。原先 tiled read 的 64 字节 descriptor 对齐不变。
