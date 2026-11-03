# Block-scaled MMA selector operand

Dense 与 ordered-sparse block-scaled MMA 为 A/B 各保留一个 `{byte_id, thread_id}`
selector，逻辑值为 U16。Register leaf 按同宽 integer/bit compatibility 仅接受
fundamental `.b16`、`.s16`、`.u16` carrier，各 leaf 可独立混用这些类型与合法 unsigned
immediate。不 cast/promote 声明，也不推断运行时 register 内容。`.f16`、其他宽度、
predicate/special register 及 instruction-only alternate 声明不会因此兼容，
该规则不引入 ld/st/cvt widening。

该 metadata 集含110个 variant、220个 typed A/B selector contract，覆盖 dense/
ordered-sparse MXF8F6F4（1X）、MXF4（2X）、MXF4NVF4（2X、4X）。这些是 metadata
inventory 数，不是已执行 program 或数值运算 coverage 数。每个 selector 仍恰含两个
scalar register/immediate leaf。显式普通 vector component 保留声明 SymbolId、可选
compact-member index、scalar lane 与 source range。Whole vector 不能替代单个 leaf
或整个 tuple；nested constructor 与其他 cardinality 仍不支持。其他 operand family 的
真实 named vector root 保留既有合同。

Immediate conversion 仍为 unsigned U16，并保留原 source bits。1X/2X/4X 的 byte mask
依次为 `0b1111`、`0b0101`、`0b0001`；A thread ID 为0–1，B 为0–3。Negative、floating
和 overflowing literal 仍非法。允许 signed register carrier 不等于允许 negative
immediate syntax、constant propagation 或 runtime bounds proof。独立 sparse metadata
selector、fragment、B32 scale-data role、modifier、target 与 availability 不变。

Source resolution、source-aware validation 与 AST-free owned checking 共享这些 typed
contract。复制/clone 后的 owned payload 可进行 coherent 当前 B16/S16/U16 identity
retarget；错误 width/kind/whole-vector shape 或不一致的 cached type/component/member
provenance 会被拒绝。Source association 单独检查所提供 syntax。不推导 literal 表示、
allocation、GPU execution 或尚未支持的 named matrix-fragment 扩展。
