# 扩展精度整数覆盖

算术 YAML 按照
[PTX 9.3 扩展精度算术规范](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#extended-precision-arithmetic-instructions)
建模 §9.7.2 全部 48 个 type/mode/effect 组合：`add.cc`、`addc{.cc}`、
`sub.cc`、`subc{.cc}`、`mad.{hi|lo}.cc` 与 `madc.{hi|lo}{.cc}` 的
`.u32/.s32/.u64/.s64` 形式。既有 add/sub 的 32 位形式要求 PTX 1.2，64 位
形式要求 PTX 4.3 和 SM 20。`mad`/`madc` 的 32 位形式要求 PTX 3.0 和 SM 20，
64 位形式要求 PTX 4.3 和 SM 20。它们必须显式指定 `.hi` 或 `.lo`，不建模隐式
默认 mode。舍入、饱和、wide 与 16-bit form 不合法。三个 multiply-add source
均接受寄存器或整数 immediate，沿用 same-width integer policy。

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
| `mad.hi.cc` / `mad.lo.cc` | `CarryOut` |
| `madc.hi` / `madc.lo` | `CarryIn` |
| `madc.hi.cc` / `madc.lo.cc` | `CarryInOut` |

进位和借位使用同一个隐式架构位 `CC.CF`。执行谓词同时控制显式结果和
隐式状态影响；该位不跨函数调用保留。前端保留这些语义契约，不执行算术，
也不维护模拟器状态。消费者不能根据 opcode 拼写猜测状态影响，也不能假设
输入位已初始化。effect 由生成的 variant identity 固定，不是可变 instruction metadata。

checked-in 的 [CUDA 13.1 `ptxas` evidence](../extended_precision_ptxas.json)
包含 32 个 self-contained case：11 个 accept、14 个 contract reject、3 个 omitted-mode
observation reject、2 个 version boundary、1 个 target boundary 与 1 个 tool-unavailable。
其中还记录 2 个 assembler 接受 duplicate `.cc` 的 observation；frontend 仍要求每个 modifier
最多出现一次。

专门的 C++ 测试覆盖全部类型和状态影响、谓词保留、目标版本边界、required mode、非法
modifier/layout 与 owned typed IR。安装包消费者直接读取类型化 `Mad`/`Madc` effect，并再次
验证被修改的 type，不依赖 AST 或 source-spelling dispatch。

## 生成规模与本地检查

canonical database 从 81 个 opcode、476 个 variant 增至 82 个 opcode、488 个 variant。
24 个新增文档 type/mode/effect form 对应 12 个 canonical variant：4 个追加的 `Mad`
carry-out variant，以及 8 个 `Madc` carry-in 或 carry-in/out variant。既有 `Mad` 的
variant name 与 index 保持稳定。生成的 resolved-IR subtree 仍为 13 个文件和 9 个 C++
source；大小从 18,444,809 增至 18,785,864 bytes，C++ 行数从 381,098 增至 388,329
（+341,055 bytes、+7,231 lines）。

最终 Debug build 在既有已配置、启用 cache 的 `out/build/ci-linux-gcc-debug` tree 中运行
75 个 Ninja action，耗时 57 秒，并非 clean build。完整 Python discovery 通过 222 个测试，
耗时 55.392 秒。普通 Debug CTest 排除单独运行的 consumer 后为 886/886，耗时 11.58 秒；
installed-package consumer 为 1/1，耗时 190.27 秒。Release validation 留给 CI。
