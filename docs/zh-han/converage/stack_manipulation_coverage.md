# 栈操作覆盖

canonical catalogue 实现 PTX ISA 9.3 [§9.7.18 Stack Manipulation Instructions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#stack-manipulation-instructions) 的 `stacksave`、`stackrestore`、`alloca`，均支持 `.u32` 和 `.u64`。分配具有省略与显式 `immAlign` 两种 layout，共八个源码形式。所有形式要求 PTX 7.3、`sm_52`，支持 entry/device 函数及两种模块地址宽度。target registry 显式包含 generic `sm_50` 与 `sm_52`，不赋予额外 capability 或 family feature，使模块验证能够区分栈指令门槛与未知 target。

`StackInstructionDescriptor` 公开不可变的操作、位宽和八字节默认最小对齐。`ResolvedStackToken` 拥有 register reference 和函数上下文，表示运行时不透明的栈位置。`ResolvedLocalAllocationResult` 拥有相同上下文及 local 地址空间角色。两者接受与指令同宽的标量 integer/bit register，均不冒充通用地址表达式。AST 销毁后，模块验证仍会将 wrapper 上下文及嵌套 register binding 与所属函数交叉检查。无上下文的 standalone fragment 保持未知函数上下文，不能替代完整绑定的模块。显式 partial `ResolveContext` 可以保留已绑定 register，同时省略所属函数 scope。individual checker 核验 function-kind/scope pair 的内部一致性，但没有 symbol table，不能认证 binding identity；完整模块检查补足该证明，并拒绝 partial 或未绑定 payload。

分配字节数使用普通 typed `RegOrImm` unsigned 位宽转换。零合法，但不证明运行时分配有效。size 的 register/immediate 支持依据 [§4.3.2](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#operands) 通用 operand domain 与 `alloca` 的 unsigned value 描述综合推断，官方例子只展示 register。

显式对齐是使用类型为 `.u32` 的 integer constant。根据 [§4.5.1 Integer Constants](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#integer-constants) 和 [alloca 合同](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#alloca)，前端先求值 64-bit 源常量，再转换到独立的 unsigned 32-bit alignment use，最后检查转换后值为非零、至多 `2^23` 的二次幂。因此 `4294967297` 与 `-4294967295` 转换到一并接受；`4294967296` 转换到零并拒绝。IR 保留原始 integer bits 和 negativity，checker 会再次核验转换。这是通用常量转换规则与指令合同的综合解释，CUDA 13.3 `ptxas` 提供旁证。

省略对齐与显式八字节保持不同的 source-layout presence。实际对齐至少为请求/默认对齐与运行时 frame alignment 的最大值；前端不知道 frame alignment，不声称精确实际对齐。静态检查不模拟分配、栈内存、栈溢出、token 来源、token 寿命、restore 有效性或跨 CFG 的栈纪律。

实现通过 canonical YAML metadata、normalize、typed lowering、generated public forms 与 checker projection 传递合同，不通过运行时 raw-opcode 分支定义栈语义。`test_stack_manipulation.cpp` 检查 AST-free owned module、源码形式、门槛、转换、可变 payload 与上下文完整性；`examples/stack_consumer` 检查安装后的 public headers 和 owned lifetime。运行时执行仍属下游工作；本族覆盖不等于完整 PTX 覆盖。
