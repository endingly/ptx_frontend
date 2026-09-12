# 扩展精度加减法覆盖

算术 YAML 按照
[PTX 9.3 扩展精度算术规范](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions)
建模 `add.cc`、`addc{.cc}`、`sub.cc` 和 `subc{.cc}` 的
`.u32/.s32/.u64/.s64` 形式。32 位形式要求 PTX 1.2；64 位形式要求
PTX 4.3 和 SM 20。舍入和饱和修饰符不合法。源操作数接受寄存器或整数
立即数，沿用现有整数类型和寄存器宽度策略。

每个生成变体都公开静态 `ConditionCodeEffect condition_code_effect`；
对应的 `ResolvedVariantDescriptor` 携带相同的类型化值。
普通算术指令默认值为 `None`。

| 形式 | 语义 |
| --- | --- |
| `add.cc` | `CarryOut` |
| `addc` | `CarryIn` |
| `addc.cc` | `CarryInOut` |
| `sub.cc` | `BorrowOut` |
| `subc` | `BorrowIn` |
| `subc.cc` | `BorrowInOut` |

进位和借位使用同一个隐式架构位 `CC.CF`。执行谓词同时控制显式结果和
隐式状态影响；该位不跨函数调用保留。前端保留这些语义契约，不执行算术，
也不维护模拟器状态。消费者不能根据 opcode 拼写猜测状态影响，也不能假设
输入位已初始化。`mad.cc` 和 `madc` 不在此次变更范围内。

专门的 C++ 测试覆盖全部类型和状态影响、谓词保留、目标版本边界、非法
修饰符/布局和被修改的类型化 IR。安装包消费者直接读取类型化状态影响，
并再次验证被修改的类型，不依赖 AST 或源拼写分发表。
