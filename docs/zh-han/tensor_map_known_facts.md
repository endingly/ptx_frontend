# 调用者已知 tensor-map 事实

frontend 通过已安装的 `ptx_tensor_map_known_facts.hpp` 头文件和 Resolved IR 库
提供条件查询。Python 目录和查询提供对应的规范级结果。
`project_tensor_known_access_context` 从选中的精确 `Instruction` 形式和 checker 上下文
复制类型化上下文；tensor 元数据不合格时返回诊断。非 tensor 指令既不产生
access，也不产生诊断。

tensor-map 操作数仍指向不透明的 128 字节对象。`TensorMapKnownFacts` 保存调用者独立
提供的断言；它不解码对象，也不沿控制流跟踪 `tensormap.replace`。事实值自有所有权，
不依赖 AST 生命周期。`TensorKnownAccessContext` 适配器从选中的类型化指令
得到，而不能解析 variant 名称字符串。方向、模式、rank、坐标角色、可选 reduction
操作、目标/group 拓扑及真实目标可用性，与 descriptor 断言分别保存。已知 S32 坐标
和 U16 im2col 信息在**指令使用处转换后**检查；原字面量位在可用时仍予保留。运行时
值未知的寄存器继续保持未知。

查询针对 **22 个**规则族分别返回 `Checked`、`Violated`、`Unresolved` 或
`NotApplicable`。`Checked` 仅证明已提供数值之间的一项关系，不证明 map 字节、
地址所有权、GPU 边界或同步。`Unresolved` 明确指出缺失事实或文档未给出的关系；
`NotApplicable` 表示此规则不适用于当前访问。无效枚举、维度数量、源码位与转换值
不一致、矛盾的调用者断言会在依赖查询前产生诊断。缺失值不等于已知零。

规则族覆盖：编码元素与方向、reduction 操作/类型、tiled box 字节与元素步长、
packed fill/操作/几何/swizzle 限制、interleave、swizzle atomicity/共享目标对齐/
重复图案偏移、96 字节 swizzle、128 字节的 8 字节 flip、精确 `sm_103a`
B6p2x16 store 覆盖规则、精确 `sm_120a` cluster 排除、store 坐标及 corner 符号、
im2col 形状与 U16 信息、W/W128、四行 gather/scatter，以及规范选中形式的可用性。
`sm_103a` store 规则**取代**与它冲突的通用 packed 要求，不把两者相与。目标及
PTX 版本结果须消费既有 variant 与修饰值可用性查询，包括 Table 63 在 PTX 9.0
给 W-cluster 的 `sm_110f` family；聚焦目录不复制目标 DNF、reduction 矩阵或
Table 33 编码表。

元素计数的完整维度、元素步长、字节单位的 global/tensor stride、packed
`Box-Size[0]`/`Tensor-Size[0]`、总访问 box 字节数、MAP 对象地址、global 数据基址、
访问 box 地址及 shared 目标地址是不同的可选输入。im2col 的 spatial lower 与 upper
corner 是从**相对两侧边缘**量取的有符号偏移，不是两个绝对端点；数组长度是
rank−2。W 宽度、halo、offset、pixels-per-column 和 channels-per-pixel 保持各自的
元素计数角色。不猜测 packed 元素转字节公式、replacement stride 映射或 corner
端点包含性变换。固定 PTX 规则未决定的零维度或零 pixel count 返回未分类的几何
义务；Driver Encode 限制仍仅属于 API 输入。

Table 33 的 code 15 保持一个原始码，但 load/store 有不同解释。调用者分别提供
既有 Table 33 类型枚举、原始码及规范字段投影的结果。元素断言还可提供 code 15
的两种封闭语义之一：load/prefetch 或 store/reduce。解释缺失时，元素身份及依赖的
packed 几何保持 `Unresolved`；与选中方向相反的解释违反方向身份规则，且不能据此
选择 packed 子类型。投影被拒绝会产生诊断，但不证明 map 字节。C++ 通过既有
Table 33 helper 检查枚举与原始码的一致性，不建立第二份编码表。C++
渲染器把小型公开规则元数据头与私有、非内联的查询源文件分开；两者均已进入标准生成计划。
即使提供有效的原始
atomicity code，无 swizzle 的 atomicity 使用仍为 `NotApplicable`。精确
`sm_120a` cluster load **实际使用** atomicity 时违反规则；仅有原始 code 时，
active use 仍未确定。reduction 类型成员检查复用既有操作/类型表。独立提供的
descriptor 解释可与 U32/S32/U64/S64/F16/BF16/F32 的精确同名值比较；FTZ、
TF32 与 B32/B64 没有已获批准的通用转换或 descriptor-code 桥梁。未知 active
atomicity、运行时 peer/mask 成员资格、barrier 完成和 descriptor 内容仍是义务。

固定 PTX 9.3 手册没有给出完整的原始 128 字节 tensor-map 字段布局。原始对象解码
不属于 #152 前端收口；若要实现，须另行明确字节布局范围，调用者已知事实查询
**不能**代替它。选定 tensor
形式已支持规范 [cache-hint/policy 操作数](tensor_cache_controls.md)，它与 descriptor
事实独立。准确形式、证据与剩余义务见 [tensor async 覆盖](converage/tensor_async_coverage.md)
和 [frontend 收口矩阵](converage/tensor_async_closeout.md)。

## 完整关系的保守边界

以下五个规则 ID 在局部检查通过后仍有意保持 `Unresolved`，即使调用者事实 API 支持的全部相关字段都有已知值。提供数值不等于提供缺失的数学关系，也不能证明来源。报告可以既没有不合格断言诊断，也没有 `Violated` 行，却仍包含这些未解决义务；这里不存在 descriptor 整体已验证的汇总结果。

| 规则 ID | 已实现的局部检查 | 仍为 `Unresolved` 的完整关系 | 相关且受支持的调用者数值 |
| --- | --- | --- | --- |
| 17 `STORE_BOUNDS` | Store/reduce 范围、转换后坐标非负、lower-edge 偏移非负与 upper-edge 偏移非正；无偏移 im2col store 需要两组边缘数组。 | 整个 tiled/store box 在完整 tensor 内的包含关系与相对两侧边缘的变换。 | 完整/tiled 维度、traversal、坐标及适用的边缘数组。 |
| 18 `IM2COL_SHAPE` | Im2col 范围、rank 3–5、spatial 长度 rank−2，以及对应 rank 的有符号边缘偏移范围。 | Filter-base/spatial-box 的相对两侧边缘关系，包括所选 spatial 坐标的来源。 | 完整/tiled 维度、traversal、已知坐标、spatial box 和两组边缘数组；仅有坐标数值不能证明来源。 |
| 19 `IM2COL_INFO` | 普通 im2col 范围、rank 3–5、对应 rank 的 information 数量和转换后 U16 范围；请求 pixel 过多且 fill 未知时保持未解决。 | Pixels/traversal/available-NDHW/fill 关系。 | 维度、traversal、坐标、information、每列 pixel 数、每 pixel channel 数、available NDHW 数量及 fill。 |
| 20 `W_PROFILE` | W/W128 范围、rank 3–5、无 interleave、要求 swizzle、实际使用时排除 128-byte/flip8、D/H extent 为一及既定 information 范围。 | W/W128 的完整宽度、相对两侧边缘、边界和数量关系。 | 维度、traversal、坐标、spatial box、information、W 宽度和边缘偏移、数量、fill 及布局断言。 |
| 21 `GATHER_SCATTER` | Gather4/scatter4 范围、rank 为二、五个坐标角色、无 interleave 且 tiled-box 高度为一。 | 每个所选 row 的完整边界/extent 关系。 | 完整/tiled 维度、traversal 及五个角色的已知坐标；API 不新增逐 row extent 或来源事实。 |

已知的局部反例仍为 `Violated`；结构上不适用仍为 `NotApplicable`。缺失或未知字段保持未知，不能当作已知零。这些完整关系限制不会改变 22 个稳定 ID、依赖 mask 或不合格断言的隔离规则；未解决 detail 中提到的字段不会自动成为新的 metadata 依赖。

这是文档化的范围边界，不是实现了五个求解器。完整的受支持调用者数值或通过的局部检查都不证明 tensor-map 字节、实际 GPU 边界、地址所有权、同步或运行时协议完成。既有局部 `Checked` 结果仍仅具有其明确的条件含义。
