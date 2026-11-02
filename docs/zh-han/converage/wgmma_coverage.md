# PTX 9.3 WGMMA 覆盖范围

Frontend 将五种 WGMMA 拼写建模为 2,151 个精确逻辑形式：1,092 个 dense `wgmma.mma_async`、1,056 个 sparse `wgmma.mma_async.sp`，以及 `wgmma.fence`、`wgmma.commit_group`、`wgmma.wait_group` 各一个。规范数据来自安装包中的 `asynchronous_warpgroup_matrix_multiply_accumulate.yaml`。全部形式都要求 `sm_90a` target family。Dense MMA 与控制指令从 PTX 8.0 开始，sparse MMA 从 PTX 8.2 开始，混合 `.s8.u8` / `.u8.s8` 从 PTX 8.4 开始。普通 `sm_90` 不满足 `sm_90a` 要求。

Dense 形式覆盖 `.f16`、`.bf16`、`.tf32`、FP8 `.e4m3`/`.e5m2`、可带 `.satfinite` 的 `.s8`/`.u8` 整数形式，以及 `.b1.and.popc`。Sparse 形式覆盖对应浮点与整数类型，使用一个 `.b32` metadata 寄存器和按类型限制的 selector：`.f16`/`.bf16` 与 `.tf32` 可取 0 或 1，FP8 与整数只能取 0。浮点 A/B immediate scale 必须分别为 -1 或 1；`.f16`/`.bf16` descriptor 形式还暴露合法 transpose control。A 来源可以是不透明的 64 位 shared-memory descriptor，也可以是四寄存器 fragment；B 始终是 shared-memory descriptor。D 寄存器 fragment 可读写。其数量由 shape 和 accumulator 类型决定，N=256 时 packed `.f16` 为 64 个，`.f32`/`.s32` 为 128 个。普通 vector 与 selector operand 的限额独立保留。

`MatrixInstructionDescriptor` 保留 M/N/K、element 与 register 类型、row/column placement、精确 fragment 数量、A 来源位置、sparse metadata kind 和 `WarpGroup128` participation identity。`ResolvedSharedMatrixDescriptor` 保留绑定的逻辑 64 位 B64/U64/S64 标量 General carrier；运行时地址、对齐、swizzle、布局和跨 warp 一致性由生产者与使用者保证。每个 WGMMA 语义形式都是带直接类型化操作数和不可变 `matrix_topology` 的 final `Instruction` 类。MMA 类通过 `matrix_descriptor()` 返回该拓扑；protocol-control 类没有矩阵描述符。`Instruction::check()` 核对可变操作数、选中布局和目标可用性。独立的 `WgmmaGroup` completion identity 与 issue/register-fence/commit/wait action 暴露 protocol obligation；frontend 不证明动态 warpgroup 收敛、指令序列顺序、寄存器读取前完成或 GPU 数值结果。

`wgmma.wait_group` 接受非负整数常量，owned immediate 使用 `.u64`。PTX 9.3 没有给出数值上限，因此 frontend 不加入未记载的 0..7 或 32 位限制。源码符号和转换后位值都保留供 checker 核对。CUDA 13.3 `ptxas` 接受 wait count 为 2^32 的完整 module，并拒绝 `-1`；这只是抽样 assembler 证据，不构成新的 ISA 上限。

采用的工程解释：针对结束于 N=224 的不完整 dense-integer Syntax 枚举，此处采用独立 WGMMA shape 表与 integer fragment 契约的优先性。Integer N 网格为 `8,16,24,32,48,64,...,256`（32 后步长 16）；N=240/256 使用 120/128 个标量 S32 D 寄存器。两种 A placement、全部 S8/U8 配对及 plain/satfinite 形式均保留；integer N=232/248 不在网格中，N=264 超出范围。局部依据与独立 target/count 控制见[参考解释](matrix_reference_interpretations.md#dense-integer-wgmma-n-boundary)。

代码生成保留全部 2,151 个精确类 identity，并划分为有界的 64-form shard。Public catalogue getter 返回由静态存储支持的 span，不依赖 source AST 生命周期。[安装包 matrix consumer](../../../examples/matrix_consumer/README.md) 测试跨 shard 边界的 WGMMA 形式以及控制/MMA descriptor 的区别。

分别运行 C++ 和 Python 检查；已配置的 CTest preset 不包含 `python_test`：

```sh
PYTHONPATH="$PWD/python/src" ctest --preset ci-linux-clang-debug --parallel 1 --output-on-failure
PYTHONPATH="$PWD/python/src" ctest --test-dir out/build/ci-linux-clang-debug -C Debug -R '^python_test$' --parallel 1 --output-on-failure
```

依据：[NVIDIA PTX ISA 9.3 的 asynchronous warpgroup-level matrix instructions](https://docs.nvidia.com/cuda/archive/13.3.0/parallel-thread-execution/index.html#asynchronous-warpgroup-level-matrix-instructions)。

[Descriptor carrier 解释](matrix_reference_interpretations.md#逻辑-descriptor-carrier-与基本位数据) 区分精确位宽的 B/U/S descriptor 角色与普通 `SameWidth` 位数据：这些角色拒绝 F64/F32，`mov.b64`/`mov.b32` 位复制仍合法。TCGEN `idesc` 是 32 位 instruction descriptor，不是 TMA im2col。Lane-mask F32 与实际 `.b64` mbarrier arrival-state 数据保留独立策略；运行时 descriptor 内容仍未获证明。

### Shared matrix descriptor carrier 中的 scalar component

既有 `SharedMatrixDescriptor` source kind 接纳 identifier 或显式 ordinary vector member。合法 `.reg .v2 .b64 A` declaration 占128位；`A.x`／`A.y`（或 `.r`／`.g`）选择一个 scalar64-bit descriptor-register carrier。这是 component extraction，不是 whole-vector descriptor 或 register-A fragment。同一 kind 规则覆盖既有 dense/sparse shared A/B 与 register-A B descriptor role，不按 opcode 或 A 位置特判。不改变 YAML type/domain 或公共 payload/API。

Binding 仍要求真实 General declaration、其既有允许的 physical integer/Bit64 type、实际 SymbolId/family index、范围内 explicit selector、base/leaf/outer range 与有效 scalar width。已知 F64／其余 Float、错误 width/class 或 InstructionOnly declaration 不被转换为 Bit word。`.v4.b64` 超过 ordinary128位 declaration 上限。`A<2>` 声明带后缀的变量而不声明 bareA；scalarA1 与 vector-familyA1.x 是不同的显式情形，不隐式选择首元素或 index。Source component 需要既有 declaration context，不猜 standalone width/SymbolId。

Shared scalar descriptor（`IdentifierRef|VectorMember`）与 whole named matrix fragment（`IdentifierRef|VectorPack`）的 syntax 交集仍仅为 IdentifierRef。公共 syntax-only ambiguity、独立建立的 for-all layout-pair certificate 与 private actual-declaration selection 仍独立。错误 whole vector 或32-bit component 不得 fall through 后被 scalar64 descriptor 分支救活。其余 scalar/member mapping、fragment member 权限、partial-context 优先级与 target policy 均不变。

Current owned validation 重新关联实际 component fact 与 declaration；合法的一致 retarget 是数据修改，而矛盾事实在有健康 supplied syntax 时仍失败。Saved source identity 则独立拒绝真正不同的合法 operand AST。既有 descriptor view／collector 与同步 observer 在 source 销毁、copy／assignment／move／function copy／Instruction clone 后继续传递实际 RegRef/range，且每个 owner 重新取得 borrow。该 source-admission 契约不证明 runtime descriptor bit、shared addressing、同步或 matrix 运算。记录 delivery 完成前仍需实际 current-parent Debug／生成 syntax／public-link 证据及独立 actual-diff 验收。

当前 module validation 也会在 availability-context skip 前，按既有128位 declaration 上限检查 selected register 的完整 owner。coherent256位 current owner 在两个 module validation API 中均于 component range 报出容量专用诊断；缺少 target context 不能隐藏此已知失败。既有实际 SymbolTable join 仍拒绝矛盾或缺失的 cached facts。本检查不新增 standalone instruction-check 保证。
