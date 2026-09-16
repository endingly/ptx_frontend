# 整数算术覆盖情况

frontend 已建模 PTX ISA 9.3 §9.7.1 文档列出的全部 161 个 type/mode 组合：`add`、`sub`、
`mul`、`mad`、`clmad`、`mul24`、`mad24`、`sad`、`div`、`rem`、`abs`、`neg`、`min`、
`max`、`popc`、`clz`、`bfind`、`fns`、`brev`、`bfe`、`bfi`、`szext`、`bmsk`、`dp4a` 与
`dp2a`。canonical source 是 `python/code_gen/resources/ptx_spec/arithmetic.yaml`；
parser descriptor、Resolved IR 和 target-aware checker 都由其生成。

文档中的 `min.s16x2.relu` example 作为 canonical `min.relu.s16x2` 的 modifier-order alias
接受；它不新增 form 或 canonical variant。

规范依据是 [archived CUDA 13.3 PTX ISA 9.3
manual](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html)。
这里描述 frontend 的 syntax/validation coverage，不表示执行语义。form count 为：add 16、sub 11、
mul 16、mad 17、clmad 2、mul24 4、mad24 5、sad 6、div 6、rem 6、abs 3、neg 4、min 13、
max 13、popc 2、clz 2、bfind 8、fns 1、brev 2、bfe 4、bfi 2、szext 4、bmsk 2、dp4a 4、
dp2a 8。

既有完整 MUL 与位操作 public variant 均保留，余下 form 追加建模。`clmad` 要求 PTX 9.3
与 `sm_80`；`fns` 要求 PTX 6.0 与 `sm_30`；`szext`、`bmsk` 要求 PTX 7.6 与 `sm_70`；
`dp2a`/`dp4a` 要求 PTX 5.0 与 `sm_61`。packed add/sub/min/max/neg form 保留文档定义的
`sm_90` 与 `sm_120f` availability。

`fns` 对 immediate `base` 检查 `0..31`；register value 在 runtime 才确定。`bmsk`
接受完整 32-bit position/width domain，因为手册为较大值定义了 clamp/wrap 行为；`szext`
也接受完整 unsigned width control。dot-product form 分别保留两个 packed input 的 signedness；
仅在两者皆 unsigned 时 destination/accumulator 为 `.u32`，其他组合均为 `.s32`。

canonical database 从 72 个 opcode、406 个 variant 增至 81 个 opcode、476 个 variant；原有
72 个 opcode 的所有 variant name 与 index 保持稳定。复用的 Debug generated subtree 共有 13 个
file，其中 9 个为 C++ source，总计 18,444,254 bytes、381,078 C++ lines；相对保留的 baseline
增加 1,611,840 bytes 与 20,801 C++ lines。direct regeneration 到 temporary output 用时 49.885 秒。

最终以 `-j 4` 构建 Debug `test_resolved_ir`，75 个 Ninja action 用时 69.733 秒；复用了已配置的
构建目录和编译器缓存，因此这不是干净构建基准。普通 Debug CTest 通过 884/884，用时 11.96 秒；
安装后的 public package consumer 通过 1/1，用时 85.75 秒。全量 Python discovery 通过 221 项测试，
用时 56.646 秒。Release 验证仍交给 CI，本次未在本地运行。

checked-in 的 [CUDA 13.1 `ptxas` evidence](../integer_arithmetic_ptxas.json)
共执行 25 个 module：11 个 accept、10 个在目标 contract boundary reject、4 个因该 toolchain
只支持到 PTX 9.1 而不可用。即使旧 assembler 接受 undefined-behavior 的 FNS input，immediate
bound 仍以规范 PTX 9.3 文本为准。
