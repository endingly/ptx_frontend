# Tensor async 缓存控制

规范 tensor `cp` 模型原有 178 个 tensor 标识和 241 个操作数布局。每个标识新增
一个 cache-hint 同级形式，每个原布局对应两个新布局，因此现在共有 356 个
tensor 标识、723 个 tensor 布局及全部 473 个 `Cp` 标识。原有 178 个标识及
其布局继续可用。

对于适用的 tensor load、store、reduction 和 prefetch，书写的
`.L2::cache_hint` 位于现有 mode、multicast 和 CTA-group 后。存在时，末尾的
`cache_policy` 操作数位于 im2col 信息与 multicast mask 之后。合法源码状态有：
无 hint/无 policy、书写 hint/无 policy、书写 hint/末尾 policy。未书写 hint
却提供 policy 无效。hint 是指令实例的源码事实，而不是 variant 的固定属性。
新同级形式继承父形式的精确目标、mode、rank、destination、multicast、group、
completion、坐标及类型化操作数契约；原有父标识与布局继续保留。

末尾 policy 使用普通带位置的 `reg_or_imm` 承载、选中 B64 并采用 `Narrow`
转换。标量 General B64、U64、S64 寄存器可用；声明类型不兼容、类别非
General、向量宽度不合或已绑定标识缺少声明均应拒绝。未绑定而类型未知的标量
仍是待判义务。整数 `0`、`1`、`18446744073709551615` 与有符号 `-1`
保留求值后的原始 64 位源码位及源码符号。后两者产生相同 B64 字，却有不同
符号来源。本契约不解码 policy 位、eviction 模式或臆造数值来源范围。policy
不修改指令 completion 或 memory-consistency 元数据。

im2col 信息可以有也可以没有，multicast mask 可以与显式 CTA group 共存。
“无 info 加 policy”与“有 info 无 policy”在操作数个数相等时，应由标量
policy 与向量 info 的形状区分，而非仅凭个数。保留已接受的 multicast/group
后缀别名，不引入任意后缀排列。选中的缓存信息在语法 AST 销毁后仍须自有值及
源码位置。直接检查、完整模块检查及引用收集仍能看到原始寄存器类别、类型、
向量宽度、绑定、源码位与符号。缓存同级形式的已知 tensor-map 事实上下文继续
表示相同的方向、rank、destination、目标及可用性。

正式 PTX 9.3 语法分别把 hint 和 policy 标成可选，仅规定 policy 必须有 hint，
因此 frontend 接受 hint-only。早期 PTXAS 13.3.73 的 32 次调用语料记录了 13 次
接受、19 次拒绝，另有一次依赖跳过；确切输入未纳入仓库。本次针对当前实现重新运行
26 次 PTXAS 13.3.73，12 次接受、14 次拒绝。五种 load/store/reduction/prefetch
方向的 hint 加 B64 policy 均成功汇编；对应的五个 hint-only 与五个无 hint 有
policy 的形式均被拒绝。补充探针接受带 policy 的 im2col 信息、两种已支持后缀顺序的
multicast 加 CTA group、B64 整数 `0`/`-1` 及 U64/S64 policy 寄存器；拒绝带
info 的 hint-only、缺少 multicast mask 和 B32 policy 寄存器。PTXAS 也把省略
im2col 信息但带末尾 policy 的 load 判为“预期 vector”，尽管规范的可选 info 语法
及 frontend 的类型化布局允许它。这些汇编观察不证明缓存效果、运行时有效性或缓存
专属目标下限。
这 [26 个完整源码模块及观察结果](../tensor_cache_controls_ptxas.json)保留了每例的
哈希、目标与重放命令；早期 32 次调用语料不能由这些文件还原。

当前 direct-class Debug 可行性检查采用 Clang 21、相同配置参数，并对变化最大的 Cp
方法分片 `_003` 各用单个编译任务。当前 main 与本次修改的源码大小分别为
2,597,968 与 4,077,717 字节，对象文件为 7,922,896 与 10,386,304 字节，耗时
6.5 与 10.0 秒，编译器 RSS 峰值为 445,016 与 521,224 KiB。两次检查均高于
主机 MemAvailable 1,572,864 KiB 的保护下限。已有依赖及缓存状态被复用，因此这些是
有界的本地测量，而非干净构建耗时。另一次本次修改后的库续编使用四个任务，耗时
289.8 秒，编译器合计 RSS 峰值 5,370,380 KiB，主机最少仍有 14,885,684 KiB
可用内存，未触发保护；它不是与 main 的整个构建对比。当前 direct-class 模型不需要
历史上的 Cp 私有分区。

公开 `query_tensor_cache_controls(const Instruction&)` 查询从选中的自有形式复制书写的 hint、可选带位置 policy、诊断
及适用性标记；它不读取不透明 tensor-map 字节，也不推断 descriptor 内容。
独立的条件 descriptor 查询见[调用者已知 tensor-map 事实](tensor_map_known_facts.md)，
支持的形式见[tensor async 覆盖](tensor_async_coverage.md)。
