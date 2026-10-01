# Tensor Memory 寄存器传输覆盖范围

前端在与 Tensor Memory 分配相同的规范指令类别中建模 PTX 9.3 的
`tcgen05.ld`、`tcgen05.st`、`tcgen05.ld.red` 和
`tcgen05.wait::{ld,st}`。加载与存储拥有不同的完成标识。wait 指向执行线程
此前发出的相应操作；已解析 IR 不表示操作已经完成。

| 操作 | 支持的控制项 | 寄存器片段 |
| --- | --- | --- |
| `ld` | 五种数据形状；受下表约束的 `.x1`–`.x128`；可选 `.pack::16b` | 可写的 32 位 General 标量寄存器花括号片段 |
| `st` | 相同形状和重复次数；可选 `.unpack::16b` | 可读的 32 位 General 标量寄存器花括号片段 |
| `ld.red` | `.32x32b` 或 `.16x32bx2`；`.x2`–`.x128`；min/max；`.f32` 可分别选用 `.abs` 和 `.NaN`，`.u32`/`.s32` 不允许这两项 | 可写花括号片段和独立的 32 位标量规约结果 |
| `wait` | 仅 `.wait::ld` 或 `.wait::st`，后接 `.sync.aligned` | 无操作数 |

形状和数量契约依据归档 PTX 9.3 表 52、53。`.32x32b`、`.16x64b` 和
`.16x32bx2` 在最高 `.x128` 的 `.xN` 形式使用 N 个寄存器；
`.16x128b` 在最高 `.x64` 时使用 2N 个；`.16x256b` 在最高 `.x32`
时使用 4N 个。此片段类别的 128 寄存器上限须显式指定。省略上限时仍为
64；普通 PTX 向量另有八元素限制。pack/unpack 不改变寄存器数量。

```ptx
.version 9.0
.target sm_110a
.address_size 64
.entry kernel() {
  .reg .b32 %r<2>;
  .reg .b32 %taddr;
  .reg .u32 %red;
  tcgen05.ld.sync.aligned.32x32b.x2.b32 {%r0, %r1}, [%taddr];
  tcgen05.wait::ld.sync.aligned;
  tcgen05.ld.red.sync.aligned.32x32b.x2.min.u32 {%r0, %r1}, %red, [%taddr];
  tcgen05.wait::ld.sync.aligned;
  tcgen05.st.sync.aligned.32x32b.x2.b32 [%taddr], {%r0, %r1};
  tcgen05.wait::st.sync.aligned;
  ret;
}
```

`[taddr]` 是简单的方括号 Tensor Memory 地址，沿用已接受的无符号 32 位
使用处转换；不接受普通方括号偏移量，也不套用其字节偏移规则。分半形状
`.16x32bx2` 带有独立整数 `immHalfSplitoff`：加载及规约放在末尾，
存储放在地址与片段之间。其拥有值保留计算后的 64 位源位、源有符号/无符号
类别和源位置。固定版本的 ISA 文本没有指定该操作数的使用宽度、单位、范围
或对齐，因此前端不推断这些属性，也不计算第二地址。AST 销毁后可检测源类别
的结构损坏，但无法仅凭此值鉴别一个合法源常量被替换为另一个。

正式规约语法把 `min` 或 `max` 放在标量类型之前，文档示例把类型放在前面。
经测试的两种顺序解析为同一类型化标识，并保留写入的修饰符位置。此别名
不接受其他排列，整数规约也不接受浮点控制项。

基础加载/存储/wait 在 PTX 8.6 起要求精确 `sm_100a`，在 8.8 起可用
`sm_100f` 家族，在 9.0 起可用精确 `sm_110a` 或 `sm_110f` 家族。
规约在 PTX 8.8 起要求 `sm_103f` 家族，在 9.0 起要求 `sm_110f`
家族；合格的精确目标通过目录继承。通用目标及无关目标不能替代这些引入条件。

前端检查写入形式、已知寄存器声明、精确片段数量、源来源信息、修饰符域和
目标配置。warp 参与、一致地址值、分配有效性、动态 wait 匹配以及控制流
中的顺序仍是运行时义务。这里没有 GPU 执行或时间证明。Tensor Memory
复制与位移仍待完成；[专用 commit 与 fence 行为](tcgen_sync_coverage.md)
具有独立的完成与排序契约。
