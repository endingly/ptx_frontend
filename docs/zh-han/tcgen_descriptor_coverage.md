# TCGEN descriptor 字段与布局覆盖

本 API 将[固定 PTX 9.3 的表 43、45–48](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)作为编码事实。调用方显式提供已知的 64 位 shared descriptor、附带 MMA kind 的 32 位 instruction descriptor，或 64 位 zero-column word。这些按值持有的 word **不**来自、不认证、也不追踪 opaque source register。copy 指令的 `TcgenCopyDescriptorView` 则借用 owning resolved register，仅检查其 source carrier；不会解码该寄存器的运行期内容。两者的生命周期与可声明的事实不同。

`ptx_tcgen_descriptors.hpp` 提供 typed 字段查询和纯 defined-field report。report 将字段违规与条件检查缺少的事实分开；不证明 allocation state、buffer extent、实际内存布局、source-register 值、producer ordering 或完整 MMA 合法性。instruction 与 zero-column source-view 类型为后续已选中的 MMA form 保留独立角色；本基础层不增加 operand grammar 或 source-value attachment。

表 43 暴露 14 位 start、leading 与 stride 编码及其低 18 位字节部分。已知字节事实按 16 字节对齐和规范的低位编码关系检查，并保留更高地址位。识别 swizzle code 0、1、2、4、6；3、5、7 非法。fixed 与 reserved 字段分别检查。八个规范 relative K/MN row 以静态 symbolic layout 数据提供；其中 `T` 在所述 normalized representation 中表示 `128/element_bits`，并非凭空定义的 4/6 位物理 packed layout。特殊 code 1 仅有适用范围明确的 MN 8×4 atom 查询，不生成第九个 relative row。普通 code 2/4/6 的 base-pattern 检查需要单独提供 repeating-pattern start：在对应的 pattern 边界上 base 必须为零；不在边界上则必须非零且符合编码公式。这既不从 matrix start 推断 pattern start，也不新增一条笼统的对齐规则。固定表 44 未对 no-swizzle/code-1 给出无歧义的 base-pattern row，因此保留 applicability obligation。

absolute leading mode 要求编码的 swizzle 为 2 且 base offset 为 0。已知事实中的 K major、A/B transpose 均为零、精确 cataloged `sm_103a`、PTX 至少 8.8 均受到检查。缺少 target、major 或 instruction context 时返回显式 obligation。这一字段检查不证明实际 K48 MMA tuple。

表 45–47 通过显式的七类 kind identity 选择字段，不从 word 猜测 kind。查询暴露适用的 A/B/D type code、scale ID/type、sparse 状态、N/M 编码范围、saturation、transpose、negate、K choice 与 WS reuse。defined-field validation 检查 fixed/reserved 字段和内部 type/scale/sparse-K 规则；不会把 dense sparse-selector 位或非 WS reuse 当成 reserved，也不会把编码的 M/N 范围误当成表 42 支持的 MMA shape。运行时 kind/shape/group/placement/target 和 source topology 合法性属于后续 MMA form。

表 48 暴露四组 start count 与 first-span flag、`generate_mask`、used/zero-column span（编码值加一）和 shift。第 39 位保留原始极性：`generate_mask` 为 true 时启用 pattern 生成；为 false 时生成全零 mask，不强制将任何 B 列置零。提供 M/N 后，M128 使用一个 mask、M64 使用两个、M32 使用四个，并相应分割 N。条件性 shift 最大值在 M32 为 16、其余为 32；缺少 M 时较紧的检查仍待完成。非活动 count/flag 字段并非 reserved-zero。第 39 位取何值都要接受 fixed/reserved 和条件字段检查。固定表未定义 bits62–63：report 单独保留这些 **unclassified** 位，不声称完整的 64 位 descriptor 合法性。

此字段 API 是部分检查，不构成 Tensor Memory 完整合法性证明。离线组装指令不能证明调用方提供的 bit 与运行期寄存器或内存布局一致。
