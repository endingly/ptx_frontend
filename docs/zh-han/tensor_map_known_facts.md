# 调用者已知 tensor-map 事实

frontend 通过已安装的 `ptx_tensor_map_known_facts.hpp` 头文件和 Resolved IR 库
提供条件查询。Python 目录和查询提供对应的规范级结果。
`project_tensor_known_access_context` 从选中的 `Cp` variant 和 checker 上下文
复制类型化上下文；元数据不合格时返回诊断。

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

固定 PTX 9.3 手册没有给出完整的原始 128 字节 tensor-map 字段布局。完整原始对象
解码仍是独立、未解决的范围项，**不能**由调用者已知事实查询代替。Cache-hint/
policy 规范操作数仍属后续工作。已交付的指令形式与工具差异
参见当前的 [tensor async 覆盖](tensor_async_coverage.md)。
